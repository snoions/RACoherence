// main.cpp
#include <thread>

#include "config.hpp"
#include "cacheAgent.hpp"
#include "logBuffer.hpp"
#include "memLayout.hpp"
#include "user.hpp"

std::atomic<bool> complete {false};
CXLPool cxl_pool;
PerNode<NodeLocalMeta> node_local_meta;

int main() {
    std::vector<std::thread> user_group;
#ifndef PROTOCOL_OFF
    std::vector<std::thread> cacheAgent_group;
#endif
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_user = [=](unsigned uid) {
            User user(i, uid, cxl_pool, node_local_meta[i]);
            user.run();
        };
        for (int j=0; j<WORKER_PER_NODE;j++)
            user_group.push_back(std::thread{run_user, j});
    }
#ifndef PROTOCOL_OFF
    for (unsigned i=0; i<NODE_COUNT; i++) {
        auto run_cacheAgent = [=](){
            CacheAgent cacheAgent(i, cxl_pool, node_local_meta[i]);
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
    return 0;
}

