// main.cpp
#include <thread>

#include "config.hpp"
#include "cacheAgent.hpp"
#include "logBuffer.hpp"
#include "memLayout.hpp"
#include "user.hpp"

thread_local unsigned node_id;
thread_local unsigned user_id;
CXLPool cxl_pool;
PerNode<NodeLocalMeta> node_local_meta;

int main() {
    std::vector<std::thread> user_group;
    std::vector<std::thread> cacheAgent_group;
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_user = [=](unsigned uid) {
            node_id = i;
            user_id = uid;
            User user(cxl_pool.meta, cxl_pool.data, node_local_meta[node_id].cache_info, node_local_meta[node_id].user_clock);
            user.run();
        };
        for (int j=0; j<WORKER_PER_NODE;j++)
            user_group.push_back(std::thread{run_user, j});
    }
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_cacheAgent = [=](){
            node_id = i;
            user_id = WORKER_PER_NODE;
            CacheAgent cacheAgent(cxl_pool.meta.bufs, node_local_meta[node_id].cache_info);
            cacheAgent.run();
        };
        cacheAgent_group.push_back(std::thread{run_cacheAgent});
    }
    for (unsigned i=0; i<user_group.size(); i++)
        user_group[i].join();
    for (unsigned i=0; i<cacheAgent_group.size(); i++)
        cacheAgent_group[i].join();
    return 0;
}

