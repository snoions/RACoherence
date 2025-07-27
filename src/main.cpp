// main.cpp
#include <thread>

#include "config.hpp"
#include "cacheAgent.hpp"
#include "memLayout.hpp"
#include "numaUtils.hpp"
#include "user.hpp"
#include "workload.hpp"

std::atomic<bool> complete {false};
thread_local unsigned node_id;
thread_local unsigned user_id;

int main() {
#ifdef USE_NUMA
    run_on_local_numa();
    char *cxl_pool_buf = (char *)remote_numa_alloc(sizeof(CXLPool));
#else 
    char *cxl_pool_buf = new char[sizeof(CXLPool)];
#endif

    CXLPool *cxl_pool = new (cxl_pool_buf) CXLPool();

    char *node_local_meta_buf = new char[sizeof(NodeLocalMeta) * NODE_COUNT];
    NodeLocalMeta *node_local_meta = new (node_local_meta_buf) NodeLocalMeta[NODE_COUNT];

#ifdef SEQ_WORKLOAD
    SeqWorkLoad workload;
#else
    RandWorkLoad workload;
#endif

    std::vector<std::thread> user_group;
#ifndef PROTOCOL_OFF
    std::vector<std::thread> cacheAgent_group;
#endif
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_user = [=, &workload] (unsigned uid) {
            node_id = i;
            user_id = uid;
            User user(*cxl_pool, node_local_meta[i]);
            user.run<decltype(workload)>(workload);
        };
        for (int j=0; j<WORKER_PER_NODE;j++)
            user_group.push_back(std::thread{run_user, j});
    }

#ifndef PROTOCOL_OFF
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_cacheAgent = [=](){
            //TODO: make sure these cores are on local NUMA
#ifdef CACHE_AGENT_AFFINITY
            set_thread_affinity(i);
#endif
            node_id = i;
            CacheAgent cacheAgent(*cxl_pool, node_local_meta[i]);
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

#ifndef USE_NUMA
    delete[] cxl_pool_buf;
#endif
    delete[] node_local_meta_buf;
    return 0;
}

