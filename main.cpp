// main.cpp
#include <thread>

#include "config.hpp"
#include "copier.hpp"
#include "worker.hpp"
#include "logBuffer.hpp"
#include "types.hpp"

thread_local unsigned node_id;
LogBuffer buffers[NODECOUNT];

int main() {
    std::vector<std::thread> worker_group;
    std::vector<std::thread> copier_group;
    for (unsigned i=0; i<NODECOUNT; i++) {
        auto run_worker = [=]() {
            node_id = i;
            Worker worker(&buffers[node_id]);
            worker.run();
        };
        for (int j=0; j<WORKER_PER_NODE;j++)
            worker_group.push_back(std::thread{run_worker});
    }
    for (unsigned i=0; i<NODECOUNT; i++) {
        auto run_copier = [=](){
            node_id = i;
            Copier copier(buffers);
            copier.run();
        };
        copier_group.push_back(std::thread{run_copier});
    }
    for (unsigned i=0; i<worker_group.size(); i++)
        worker_group[i].join();
    for (unsigned i=0; i<copier_group.size(); i++)
        copier_group[i].join();
    return 0;
}

