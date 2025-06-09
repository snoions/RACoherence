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
    for (unsigned i=0; i<NODECOUNT; i++)

        worker_group.push_back(std::thread{[=](){
            node_id = i;
            Worker worker(&buffers[node_id]);
            worker.run();
        }});
    for (unsigned i=0; i<NODECOUNT; i++)
        copier_group.push_back(std::thread{[=](){
            node_id = i;
            Copier copier(buffers);
            copier.run();
        }});
    for (unsigned i=0; i<NODECOUNT; i++)
        worker_group[i].join();
    for (unsigned i=0; i<NODECOUNT; i++)
        copier_group[i].join();
    return 0;
}

