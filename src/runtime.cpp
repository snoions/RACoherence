#include <thread>
#include <atomic>
#include "cacheAgent.hpp"
#include "cxlMalloc.hpp"
#include "logger.hpp"
#include "numaUtils.hpp"
#include "runtime.hpp"

thread_local ThreadOps *thread_ops;
std::atomic<bool> complete {false};
std::atomic<unsigned> curr_tid {0};
std::vector<std::thread> cacheAgent_group;
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
        LOG_DEBUG("rac_load" << size << ":addr = " << addr) \
        if (thread_ops && in_cxl_nhc_memory(addr)) { \
            thread_ops->check_invalidate((char *)addr); \
        } \
        return *((uint ## size ## _t*)addr); \
    }

#define RACSTORE(size) \
    void rac_store ## size(void * addr, uint ## size ## _t val, const char * /*position*/) {  \
        LOG_DEBUG("rac_store" << size << ":addr = " << addr << " " << (uint64_t) val) \
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

//TODO: change node id to non thread-local
void *rac_thread_func_wrapper(void *arg) {
    auto args = (RACThreadArgs *)arg;
    thread_ops = new ThreadOps(log_mgrs, &cache_infos[args->nid], args->nid, curr_tid.fetch_add(1, std::memory_order_relaxed));
    void* ret = args->func(args->arg);
    delete thread_ops;
    return ret;
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
#if defined(CACHE_AGENT_AFFINITY) && defined(USE_NUMA)
            find_cpu_on_numa(cpu_id, LOCAL_NUMA_ID)
#elif defined(CACHE_AGENT_AFFINITY)
            cpu_id = i;
#endif
        auto run_cacheAgent = [=](){
#ifdef CACHE_AGENT_AFFINITY
            set_thread_affinity(cpu_id);
#endif
            CacheAgent cacheAgent(cache_infos[i], log_mgrs, i);
            cacheAgent.run();
        };
        cacheAgent_group.push_back(std::thread{run_cacheAgent});
    }
#endif

}

void rac_shutdown() {
#ifndef PROTOCOL_OFF
    complete.store(true);
    for (unsigned i=0; i<cacheAgent_group.size(); i++)
        cacheAgent_group[i].join();
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
