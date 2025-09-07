#include <atomic>
#include <pthread.h>
#include <unordered_map>
#include "cacheAgent.hpp"
#include "cxlSync.hpp"
#include "logger.hpp"
#include "numaUtils.hpp"
#include "runtime.hpp"

thread_local ThreadOps *thread_ops;
std::atomic<bool> complete {false};
std::atomic<unsigned> curr_tid {0};
//TODO: change to pthread for uniformity
pthread_t cacheAgent_group[NODE_COUNT];
char *cxl_nhc_buf;
char *cxl_hc_buf;
char *node_local_buf;
CacheInfo *cache_infos;
LogManager *log_mgrs;

inline bool in_cxl_nhc_memory(void *addr) {
    return addr >= cxl_nhc_buf && addr < cxl_nhc_buf + CXL_NHC_RANGE;
}

//TODO: check CXL memory ranges
#define RACLOAD(size) \
    uint ## size ## _t rac_load ## size(void * addr, const char * /*position*/) { \
        if (thread_ops && in_cxl_nhc_memory(addr)) { \
            thread_ops->check_invalidate((char *)addr); \
        } \
        return *((uint ## size ## _t*)addr); \
    }

#define RACSTORE(size) \
    void rac_store ## size(void * addr, uint ## size ## _t val, const char * /*position*/) {  \
        if (thread_ops && in_cxl_nhc_memory(addr)) { \
            thread_ops->check_invalidate((char *)addr); \
            thread_ops->log_store((char *)addr); \
        } \
        *((uint ## size ## _t*)addr) = val; \
    }

RACSTORE(8)
RACSTORE(16)
RACSTORE(32)
RACSTORE(64)

RACLOAD(8)
RACLOAD(16)
RACLOAD(32)
RACLOAD(64)

struct RACThreadArg {
    unsigned nid;
    void* (*func)(void*);
    void* arg;
};

struct RACThreadRet {
    ThreadOps *ops;
    void* ret;
};

void *rac_thread_func_wrapper(void *arg) {
    auto rac_arg = (RACThreadArg *)arg;
    unsigned tid = curr_tid.fetch_add(1, std::memory_order_relaxed);
    thread_ops = new ThreadOps(log_mgrs, &cache_infos[rac_arg->nid], rac_arg->nid, tid);
    void* ret = rac_arg->func(rac_arg->arg);
    thread_ops->thread_release();
    delete rac_arg;
    auto *rac_ret = new RACThreadRet{thread_ops, ret};
    return rac_ret;
}

int rac_thread_create(unsigned nid, pthread_t *thread, void *(*func)(void*), void *arg) {
    RACThreadArg *rac_arg = new RACThreadArg{nid, func, arg};
    return pthread_create(thread, nullptr, rac_thread_func_wrapper, rac_arg);
};

int rac_thread_join(unsigned /*nid*/, pthread_t thread, void **thread_ret) {
    RACThreadRet *rac_ret;
    int ret = pthread_join(thread, (void **)&rac_ret);
    auto child_ops = rac_ret->ops;
    thread_ops->thread_acquire(child_ops->get_clock());
    if (thread_ret)
        *thread_ret = rac_ret->ret;
    delete child_ops;
    delete rac_ret;
    return ret;
}

struct CacheAgentArg {
    unsigned node_id;
    unsigned cpu_id;
};

void *run_cache_agent(void *arg) {
    auto carg = (CacheAgentArg*) arg;
#ifdef CACHE_AGENT_AFFINITY
    set_thread_affinity(carg->cpu_id);
#endif
    unsigned nid = carg->node_id;
    CacheAgent(cache_infos[nid], log_mgrs, nid).run();
    return arg;
}

void rac_init(unsigned nid) {
#ifdef USE_NUMA
    run_on_local_numa();
    cxl_nhc_buf = (char *)remote_numa_alloc(sizeof(CXLPool));
    cxl_hc_buf = (char *)remote_numa_alloc(sizeof(PerNode<LogManager>) + CXL_HC_RANGE);
#else 
    cxl_nhc_buf = new char[CXL_NHC_RANGE];
    cxl_hc_buf = new char[CXL_HC_RANGE];
#endif
    node_local_buf = new char[sizeof(CacheInfo) * NODE_COUNT];

    size_t cxl_hc_off = 0;
    static_assert(CXL_HC_RANGE > sizeof(LogManager[NODE_COUNT]));
    log_mgrs = (LogManager *) cxl_hc_buf;
    for (int i = 0; i < NODE_COUNT; i++)
        new (&log_mgrs[i]) LogManager(i);
    cxl_hc_off += sizeof(LogManager[NODE_COUNT]);

    cxlnhc_pool_initialize(cxl_hc_buf, cxl_hc_off, cxl_nhc_buf, CXL_NHC_RANGE);
    assert(CXL_HC_RANGE > cxl_hc_off);
    cxlhc_pool_initialize(cxl_hc_buf + cxl_hc_off, CXL_HC_RANGE - cxl_hc_off);
    cache_infos = new (node_local_buf) CacheInfo[NODE_COUNT];
    thread_ops = new ThreadOps(log_mgrs, &cache_infos[nid], nid, curr_tid.fetch_add(1, std::memory_order_relaxed));

#ifndef PROTOCOL_OFF
    unsigned cpu_id = 0;
    for (unsigned i=0; i<NODE_COUNT; i++) {
        int ret;
#if defined(CACHE_AGENT_AFFINITY) && defined(USE_NUMA)
        ret = find_cpu_on_numa(cpu_id, LOCAL_NUMA_ID);
        assert(!ret);
#elif defined(CACHE_AGENT_AFFINITY)
        cpu_id = i;
#endif
        auto arg = new CacheAgentArg{i, cpu_id};
        ret = pthread_create(&cacheAgent_group[i], nullptr, run_cache_agent, arg);
        assert(!ret);
    }
#endif

}

void rac_shutdown() {
#ifndef PROTOCOL_OFF
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
    for (int i = 0; i < NODE_COUNT; i++)
        cache_infos[i].~CacheInfo();
    delete thread_ops;
#ifndef use_numa
    delete[] cxl_hc_buf;
    delete[] cxl_nhc_buf;
#endif
    delete[] node_local_buf;

}
