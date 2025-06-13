// main.cpp
#include <thread>

#include "config.hpp"
#include "cacheAgent.hpp"
#include "logBuffer.hpp"
#include "memLayout.hpp"
#include "user.hpp"

thread_local unsigned node_id;
CXLMemMeta cxl_mem_meta;

int main() {
    std::vector<std::thread> user_group;
    std::vector<std::thread> cacheAgent_group;
    for (unsigned i=0; i<NODECOUNT; i++) {
        auto run_user = [=]() {
            node_id = i;
            User user(&cxl_mem_meta.buffers[node_id], &cxl_mem_meta.cache_clocks[node_id], &cxl_mem_meta.alocs);
            user.run();
        };
        for (int j=0; j<WORKER_PER_NODE;j++)
            user_group.push_back(std::thread{run_user});
    }
    for (unsigned i=0; i<NODECOUNT; i++) {
        auto run_cacheAgent = [=](){
            node_id = i;
            CacheAgent cacheAgent(&cxl_mem_meta.buffers, &cxl_mem_meta.cache_clocks[node_id]);
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

