#include <thread>

#include "config.hpp"
#include "cacheAgent.hpp"
#include "cacheInfo.hpp"
#include "logger.hpp"
#include "numaUtils.hpp"
#include "user.hpp"

std::atomic<bool> complete {false};
thread_local ThreadOps *thread_ops;

int main() {
#ifdef USE_NUMA
    run_on_local_numa();
    char *cxl_nhc_buf = (char *)remote_numa_alloc(sizeof(CXLPool));
    char *cxl_hc_buf = (char *)remote_numa_alloc(sizeof(PerNode<LogManager>) + CXL_HC_RANGE);
#else 
    char *cxl_nhc_buf = new char[sizeof(CXLPool)];
    char *cxl_hc_buf = new char[CXL_HC_RANGE];
#endif
    char *node_local_buf = new char[sizeof(CacheInfo) * NODE_COUNT];

    size_t cxl_hc_off = 0;
    static_assert(CXL_HC_RANGE > sizeof(LogManager[NODE_COUNT]));
    LogManager* log_mgrs = (LogManager *) cxl_hc_buf;
    for (int i = 0; i < NODE_COUNT; i++)
        new (&log_mgrs[i]) LogManager(i);
    cxl_hc_off += sizeof(LogManager[NODE_COUNT]);

    // allocator for user programs
    //cxlnhc_pool_initialize(cxl_hc_buf, cxl_hc_off, cxl_nhc_buf, CXL_NHC_RANGE);
    assert(CXL_HC_RANGE > cxl_hc_off);
    cxlhc_pool_initialize(cxl_hc_buf + cxl_hc_off, CXL_HC_RANGE - cxl_hc_off);
    CXLPool *cxl_pool = new (cxl_nhc_buf) CXLPool();
    CacheInfo *cache_infos = new (node_local_buf) CacheInfo[NODE_COUNT];

    std::vector<std::thread> user_group;
#ifndef PROTOCOL_OFF
    std::vector<std::thread> cacheAgent_group;
#endif

    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_user = [=] (unsigned tid) {
            thread_ops = new ThreadOps(log_mgrs, &cache_infos[i], i, tid);
            User user(*cxl_pool, i);
            user.run();
            delete thread_ops;
        };
        for (int j=0; j<WORKER_PER_NODE;j++)
            user_group.push_back(std::thread{run_user, i * WORKER_PER_NODE + j});
    }

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

    for (unsigned i=0; i<user_group.size(); i++)
        user_group[i].join();

#ifndef PROTOCOL_OFF
    complete.store(true);
    for (unsigned i=0; i<cacheAgent_group.size(); i++)
        cacheAgent_group[i].join();
#endif

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() / 1000 << "s" << std::endl;


    for (int i = 0; i < NODE_COUNT; i++)
        log_mgrs[i].~LogManager();
    for (int i = 0; i < NODE_COUNT; i++)
        cache_infos[i].~CacheInfo();
    cxl_pool->~CXLPool();

#ifndef use_numa
    delete[] cxl_hc_buf;
    delete[] cxl_nhc_buf;
#endif
    delete[] node_local_buf;
    return 0;
}

