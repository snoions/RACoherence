#include <atomic>
#include <fcntl.h>
#include <iomanip>
#include <numaif.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <x86intrin.h>

#include "cacheAgent.hpp"
#include "globalMeta.hpp"
#include "instrumentLib.hpp"
#include "logger.hpp"
#include "numaUtils.hpp"
#include "runtime.hpp"

namespace RACoherence {

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

unsigned node_id;
__thread ThreadOps *thread_ops;
std::atomic<bool> complete {false};
char *cxl_nhc_buf = (char *) ~0;  // set to invalid address before being initialized
size_t cxl_nhc_range;
char *cxl_hc_buf;
size_t cxl_hc_range;
CacheInfo cache_info;
pthread_t cache_agent;
GlobalMeta *meta;
#if TIME_STATS
std::atomic<uint64_t> thread_cycles; 
std::atomic<uint64_t> invd_msg_stall_cycles;
#endif

//TODO: only need a full thread_acquire/release if parent and child threads are on different nodes, otherwise only need to merge clocks
void *rac_thread_func_wrapper(void *arg) {
    if(numa_run_on_node(LOCAL_NUMA_NODE_ID)) {
        perror("numa_run_on_node");
        exit(EXIT_FAILURE);
    }
    cxl_alloc_thread_init();
    auto rac_arg = (RACThreadArg *)arg;
    unsigned tid = meta->curr_tid.fetch_add(1);
    thread_ops = new ThreadOps(&meta->log_mgrs[0], &cache_info, rac_arg->nid, tid);
#if TIME_STATS
    uint64_t start = __rdtsc();
#endif
#if !PROTOCOL_OFF
    thread_ops->thread_acquire(*rac_arg->parent_clock);
#endif
    void* ret = rac_arg->func(rac_arg->arg);
#if !PROTOCOL_OFF
    thread_ops->thread_release();
#endif
#if TIME_STATS
    thread_cycles += __rdtsc() - start;
    invd_msg_stall_cycles += thread_ops->invd_msg_stall_cycles;
#endif
    delete rac_arg;
    auto *rac_ret = new RACThreadRet{thread_ops, ret};
    return rac_ret;
}

int rac_thread_create(unsigned nid, pthread_t *thread, void *(*func)(void*), void *arg) {
#if !PROTOCOL_OFF
    thread_ops->thread_release();
    const VectorClock *parent_clock = &thread_ops->get_clock();
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

void *rac_get_user_root() {
    return meta->user_root;
}

CXLBarrier *rac_get_root_barrier() {
    return &meta->root_barrier;
}

void rac_subscribe_to_node(unsigned target) {
    assert(target <= 0 && target < NODE_COUNT && "invalid node_id");
    meta->log_mgrs[target].set_subscribed(node_id, true);
    wbinvd();
}

void rac_unsubscribe_from_node(unsigned target) {
    assert(target <= 0 && target < NODE_COUNT && "invalid node_id");
    meta->log_mgrs[target].set_subscribed(node_id, false);
}

bool rac_is_subscribed_to_node(unsigned target) {
    assert(target <= 0 && target < NODE_COUNT && "invalid node_id");
    return meta->log_mgrs[target].is_subscribed(node_id);
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
    CacheAgent(cache_info, &meta->log_mgrs[0], nid).run();
    return arg;
}

void alloc_cxl_memory() {
    int fd = -1;
    if (node_id == 0) {
        fd = shm_open(SHM_PATH, O_CREAT | O_EXCL | O_RDWR, 0666);
        if (fd == -1) {
            perror("shm_open");
            exit(EXIT_FAILURE);
        }
        if (ftruncate(fd, cxl_hc_range + cxl_nhc_range) == -1) {
            perror("ftruncate");
            exit(EXIT_FAILURE);
        }
    } else {
        while ((fd = shm_open(SHM_PATH, O_RDWR, 0666)) == -1) {
           if (errno != ENOENT) {
               perror("shm_open");
               exit(EXIT_FAILURE);
           }
           sleep(1);
        }
    }

    assert(CXL_HC_START + cxl_hc_range < CXL_NHC_START);
    cxl_hc_buf = (char *)mmap((void*)CXL_HC_START, cxl_hc_range, PROT_READ | PROT_WRITE,  MAP_SHARED | MAP_FIXED_NOREPLACE, fd, 0);
    if ((uintptr_t)cxl_hc_buf == -1) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }
    cxl_nhc_buf = (char *)mmap((void*)CXL_NHC_START, cxl_nhc_range, PROT_READ | PROT_WRITE,  MAP_SHARED | MAP_FIXED_NOREPLACE, fd, cxl_hc_range);
    close(fd);
    if ((uintptr_t)cxl_nhc_buf == -1) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

#if CXL_NUMA_MODE
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

//unsigned assign_to_numa(unsigned nid) {
//    unsigned numa_count = sizeof(CPU_NUMAS)/sizeof(CPU_NUMAS[0]);
//    if (numa_count > NODE_COUNT)
//        return CPU_NUMAS[nid];
//    // ceiling of NODE_COUNT/numa_count
//    unsigned nodes_per_numa = (NODE_COUNT + numa_count - 1)/numa_count;
//    // interleave nodes on NUMA nodes
//    return CPU_NUMAS[nid%nodes_per_numa];
//}

void rac_init(unsigned nid, size_t cxl_hc_rg, size_t cxl_nhc_rg, size_t root_size) {
    node_id = nid;
    cxl_hc_range = cxl_hc_rg;
    cxl_nhc_range = cxl_nhc_rg;
    alloc_cxl_memory();
    if(numa_run_on_node(LOCAL_NUMA_NODE_ID)) {
        perror("numa_run_on_node");
        exit(EXIT_FAILURE);
    }
    meta = (GlobalMeta*)cxl_hc_buf;
    if (node_id == 0) {
        size_t cxl_hc_off = 0;
        assert(cxl_hc_range > sizeof(GlobalMeta));
        cxl_hc_off += sizeof(GlobalMeta);

        assert(cxl_hc_range > cxlhc_off + root_size);
        meta->user_root = cxl_hc_buf + cxl_hc_off;
        cxl_hc_off += root_size;
        
        cxl_alloc_global_init(&meta->alloc_meta, cxl_hc_buf + cxl_hc_off, cxl_hc_range - cxl_hc_off, cxl_nhc_buf, cxl_nhc_range);
        cxl_alloc_process_init(&meta->alloc_meta);
        cxl_alloc_thread_init();

        new (&meta->log_mgrs[node_id]) LogManager(node_id);
        meta->curr_tid.store(0);

        thread_ops = new ThreadOps(&meta->log_mgrs[0], &cache_info, node_id, meta->curr_tid.fetch_add(1, std::memory_order_relaxed));
        new (&meta->root_barrier) CXLBarrier(NODE_COUNT);
        meta->started.store(true);
    } else {
        while (!meta->started.load()) {} 

        cxl_alloc_process_init(&meta->alloc_meta);
        cxl_alloc_thread_init();
        new (&meta->log_mgrs[node_id]) LogManager(node_id);
        thread_ops = new ThreadOps(&meta->log_mgrs[0], &cache_info, node_id, meta->curr_tid.fetch_add(1, std::memory_order_relaxed));
    }
    instrument_lib();

#if !PROTOCOL_OFF
    unsigned cpu_id = node_id;
    int ret;
#ifdef CACHE_AGENT_AFFINITY
    ret = find_cpu_on_numa(cpu_id, LOCAL_NUMA_NODE_ID);
    assert(!ret);
#endif
    auto arg = new CacheAgentArg{node_id, cpu_id};
    ret = pthread_create(&cache_agent, nullptr, run_cache_agent, arg);
    assert(!ret);
#endif
}

void rac_shutdown() {
#if !PROTOCOL_OFF
    complete.store(true);
    void *arg;
    int ret = pthread_join(cache_agent, &arg);
    assert(!ret);
    delete (CacheAgentArg*)arg;
#endif
    STATS(
        LOG_STATS("node " << i << " stats:");
        cache_info.dump_stats();
    )
    //print_jemalloc_stats();
#if TIME_STATS
    LOG_STATS("invalidation message stall percentage: " << std::fixed << std::setprecision(2) << (double)invd_msg_stall_cycles/thread_cycles * 100);
#endif
    delete thread_ops;
    meta->log_mgrs[node_id].~LogManager();
    if (node_id == 0) {
        meta->root_barrier.~CXLBarrier();
        shm_unlink(SHM_PATH);
        munmap(cxl_hc_buf, cxl_hc_range);
        munmap(cxl_nhc_buf, cxl_nhc_range);
    }
}

} // RACoherence
