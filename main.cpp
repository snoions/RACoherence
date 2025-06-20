// main.cpp
#include <thread>

#include "config.hpp"
#include "cacheAgent.hpp"
#include "logBuffer.hpp"
#include "memLayout.hpp"
#include "user.hpp"

thread_local unsigned node_id;
CXLMemMeta cxl_mem_meta;
PerNode<NodeLocalMeta> node_local_meta;

int main() {
    std::vector<std::thread> user_group;
    std::vector<std::thread> cacheAgent_group;
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_user = [&]() {
            node_id = i;
            User user(cxl_mem_meta.buffers, cxl_mem_meta.alocs, node_local_meta[node_id].cache_info, node_local_meta[node_id].user_clock);
            user.run();
        };
        for (int j=0; j<WORKER_PER_NODE;j++)
            user_group.push_back(std::thread{run_user});
    }
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_cacheAgent = [&](){
            node_id = i;
            CacheAgent cacheAgent(cxl_mem_meta.buffers, node_local_meta[node_id].cache_info);
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

