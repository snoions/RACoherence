#include <atomic>
#include <numaif.h>
#include <pthread.h>
#include "cacheAgent.hpp"
#include "cxlSync.hpp"
#include "instrumentLib.hpp"
#include "logger.hpp"
#include "numaUtils.hpp"
#include "jemallocPool.hpp"
#include "runtime.hpp"

namespace RACoherence {

thread_local ThreadOps *thread_ops;
std::atomic<bool> complete {false};
std::atomic<unsigned> curr_tid {0};
pthread_t cacheAgent_group[NODE_COUNT];
char *cxl_nhc_buf = (char *) ~0;// set to invalid address before being initialized
size_t cxl_nhc_range;
char *cxl_hc_buf;
size_t cxl_hc_range;
char *node_local_buf;
CacheInfo *cache_infos;
LogManager *log_mgrs;

struct RACThreadArg {
    const VectorClock *parent_clock;
    unsigned nid;
    void* (*func)(void*);
    void* arg;
};

struct RACThreadRet {
    ThreadOps *ops;
    void* ret;
};

//TODO: only need a full thread_acquire/release if parent and child threads are on different nodes, otherwise only need to merge clocks
void *rac_thread_func_wrapper(void *arg) {
    cxlnhc_thread_init();
    auto rac_arg = (RACThreadArg *)arg;
    unsigned tid = curr_tid.fetch_add(1, std::memory_order_relaxed);
    thread_ops = new ThreadOps(log_mgrs, &cache_infos[rac_arg->nid], rac_arg->nid, tid);
#if !PROTOCOL_OFF
    thread_ops->thread_acquire(*rac_arg->parent_clock);
#endif
    void* ret = rac_arg->func(rac_arg->arg);
#if !PROTOCOL_OFF
    thread_ops->thread_release();
#endif
    delete rac_arg;
    auto *rac_ret = new RACThreadRet{thread_ops, ret};
    return rac_ret;
}

int rac_thread_create(unsigned nid, pthread_t *thread, void *(*func)(void*), void *arg) {
#if !PROTOCOL_OFF
    const VectorClock *parent_clock = &thread_ops->thread_release();
#else
    const VectorClock *parent_clock = nullptr;
#endif
    RACThreadArg *rac_arg = new RACThreadArg{parent_clock, nid, func, arg};
    return pthread_create(thread, nullptr, rac_thread_func_wrapper, rac_arg);
};

int rac_thread_join(unsigned /*nid*/, pthread_t thread, void **thread_ret) {
    RACThreadRet *rac_ret;
    int ret = pthread_join(thread, (void **)&rac_ret);
    auto child_ops = rac_ret->ops;
#if !PROTOCOL_OFF
    thread_ops->thread_acquire(child_ops->get_clock());
#endif
    if (thread_ret)
        *thread_ret = rac_ret->ret;
    delete child_ops;
    delete rac_ret;
    return ret;
}

unsigned rac_get_node_id() {
    return thread_ops->get_node_id();
}

unsigned rac_get_node_count() {
    return NODE_COUNT;
}

void rac_subscribe_to_node(unsigned node_id) {
    auto my_node_id = thread_ops->get_node_id();
    assert(node_id <= 0 && node_id < NODE_COUNT && "invalid node_id");
    log_mgrs[node_id].set_subscribed(node_id, true);
    //execute wbinvd to cleaer cache
    FILE *fd = fopen(WBINVD_PATH, "r");
    if (fd == nullptr) {
        perror("unable to execute wbinvd");
        exit(EXIT_FAILURE);
    }
    fclose(fd);
}

void rac_unsubscribe_from_node(unsigned node_id) {
    auto my_node_id = thread_ops->get_node_id();
    assert(node_id <= 0 && node_id < NODE_COUNT && "invalid node_id");
    log_mgrs[node_id].set_subscribed(node_id, false);
}

bool rac_is_subscribed_to_node(unsigned node_id) {
    assert(node_id <= 0 && node_id < NODE_COUNT && "invalid node_id");
    return log_mgrs[node_id].is_subscribed(node_id);
}

struct CacheAgentArg {
    unsigned node_id;
    unsigned cpu_id;
};

void *run_cache_agent(void *arg) {
    auto carg = (CacheAgentArg*) arg;
#ifdef CACHE_AGENT_AFFINITY
    pin_to_core(carg->cpu_id);
#endif
    unsigned nid = carg->node_id;
    CacheAgent(cache_infos[nid], log_mgrs, nid).run();
    return arg;
}

void alloc_cxl_memory() {
    cxl_nhc_buf = (char *)mmap((void*)CXL_NHC_START, cxl_nhc_range, PROT_READ | PROT_WRITE,  MAP_SHARED | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
    cxl_hc_buf = (char *)mmap(NULL, cxl_hc_range, PROT_READ | PROT_WRITE,  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if ((uintptr_t)cxl_nhc_buf == -1) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    if ((uintptr_t)cxl_hc_buf == -1) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
#ifdef CXL_NUMA_MODE
    // make sure all threads use DRAM and cpu from local NUMA node
    if(numa_run_on_node(LOCAL_NUMA_NODE_ID)) {
        perror("numa_run_on_node");
        exit(EXIT_FAILURE);
    }
    // bind cxl memory buffers to CXL NUMA node
    unsigned long nodemask = 0;
    nodemask |= 1 << CXL_NUMA_NODE_ID;
    if(mbind(cxl_nhc_buf, cxl_nhc_range, MPOL_BIND, &nodemask, sizeof(nodemask) * 8, 0) < 0) {
        perror("mbind");
        exit(EXIT_FAILURE);
    }

    if(mbind(cxl_hc_buf, cxl_hc_range, MPOL_BIND, &nodemask, sizeof(nodemask) * 8, 0) < 0) {
        perror("mbind");
        exit(EXIT_FAILURE);
    }
#endif
}

void rac_init(unsigned nid, size_t cxl_hc_rg, size_t cxl_nhc_rg) {
    cxl_hc_range = cxl_hc_rg;
    cxl_nhc_range = cxl_nhc_rg;
    alloc_cxl_memory();
    node_local_buf = new char[sizeof(CacheInfo) * NODE_COUNT];

    size_t cxl_hc_off = 0;
    assert(cxl_hc_range > sizeof(LogManager[NODE_COUNT]));
    log_mgrs = (LogManager *) cxl_hc_buf;
    cxl_hc_off += sizeof(LogManager[NODE_COUNT]);

    // reserve space for cxl nhc pool as it needs to be initialized after cxl hc pool
    char *cxl_nhc_pool_buf = cxl_hc_buf + cxl_hc_off;
    cxl_hc_off += sizeof(ExtentPool);
    //align buffer to cache line
    if (uintptr_t remain = (uintptr_t)(cxl_hc_buf + cxl_hc_off) & (CACHE_LINE_SIZE-1))
        cxl_hc_off += CACHE_LINE_SIZE - remain;
    assert(cxl_hc_range > cxl_hc_off);
    cxlhc_pool_init(cxl_hc_buf + cxl_hc_off, cxl_hc_range - cxl_hc_off);
    cxlnhc_pool_init(cxl_nhc_pool_buf, cxl_nhc_buf, cxl_nhc_range);
    for (int i = 0; i < NODE_COUNT; i++)
        new (&log_mgrs[i]) LogManager(i);
    cache_infos = new (node_local_buf) CacheInfo[NODE_COUNT];
    thread_ops = new ThreadOps(log_mgrs, &cache_infos[nid], nid, curr_tid.fetch_add(1, std::memory_order_relaxed));
    instrument_lib();

#if !PROTOCOL_OFF
    unsigned cpu_id = 0;
    for (unsigned i=0; i<NODE_COUNT; i++) {
        int ret;
#if defined(CACHE_AGENT_AFFINITY) && defined(CXL_NUMA_MODE)
        ret = find_cpu_on_numa(cpu_id, LOCAL_NUMA_NODE_ID);
        assert(!ret);
#else
        cpu_id = i;
#endif
        auto arg = new CacheAgentArg{i, cpu_id};
        ret = pthread_create(&cacheAgent_group[i], nullptr, run_cache_agent, arg);
        assert(!ret);
    }
#endif
}

void rac_shutdown() {
#if !PROTOCOL_OFF
    complete.store(true);
    for (unsigned i=0; i<NODE_COUNT; i++) {
        void *arg;
        int ret = pthread_join(cacheAgent_group[i], &arg);
        assert(!ret);
        delete (CacheAgentArg*)arg;
    }
#endif
    for (int i = 0; i < NODE_COUNT; i++)
        log_mgrs[i].~LogManager();
    for (int i = 0; i < NODE_COUNT; i++) {
        STATS(
            std::cout << "node " << i << " statistics:" << std::endl;
            cache_infos[i].dump_stats()
	)
        cache_infos[i].~CacheInfo();
    }
    delete thread_ops;
    munmap(cxl_hc_buf, cxl_hc_range);
    munmap(cxl_nhc_buf, cxl_nhc_range);
    delete[] node_local_buf;
}

} // RACoherence
