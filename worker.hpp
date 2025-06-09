#ifndef _WORKER_H_
#define _WORKER_H_

#include <iostream>
#include <random>
#include <unordered_set>

#include <unistd.h>

#include "types.hpp"
#include "config.hpp"
#include "logBuffer.hpp"

static intptr_t genRandPtr() {
    std::random_device rd;
    std::uniform_int_distribution<uintptr_t> dist(0,UINTPTR_MAX);
    return dist(rd);
}

extern thread_local unsigned node_id;
class Worker {
    std::unordered_set<uintptr_t> dirty_cls;
    LogBuffer *my_buffer;

public:
    Worker(LogBuffer *buf): my_buffer(buf) {}

    void run() {
        unsigned count = 0;
        while(count < EPOCH) {
                uintptr_t addr = genRandPtr();
                uintptr_t cl_addr = addr & CACHELINEMASK;
                dirty_cls.insert(cl_addr);
                bool is_release = (rand() % 100) == 0;
                if(is_release || dirty_cls.size() == LOGSIZE) {
                    Log *curr_log;
                    while(!(curr_log = my_buffer->takeHead()))
                        //wait for copiers to finish
                        sleep(0);
                    
                    for(auto cl: dirty_cls)
                        curr_log->write(cl);
                    curr_log->publish();

                    std::cout << "node " << node_id << ": produce log " << count++ << std::endl;
                }
        }
    }
};


#endif
