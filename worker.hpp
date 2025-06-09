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
    Log *curr_log;

public:
    Worker(LogBuffer *buf): my_buffer(buf), curr_log(buf->getHead()) {}

    void run() {
        unsigned count = 0;
        while(count < EPOCH) {
                uintptr_t addr = genRandPtr();
                uintptr_t cl_addr = addr & CACHELINEMASK;
                dirty_cls.insert(cl_addr);
                bool is_release = (rand() % 100) == 0;
                if(is_release || dirty_cls.size() == LOGSIZE) {
                    auto iter = dirty_cls.begin();
                    if (!curr_log->claim())
                        //wait for copiers to finish
                        sleep(0);
                    
                    for(auto cl: dirty_cls)
                        curr_log->write(cl);

                    curr_log = my_buffer->moveHead();
                    std::cout << "node " << node_id << ": produce log " << count++ << std::endl;
                }
        }
    }
};


#endif
