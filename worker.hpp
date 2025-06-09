#ifndef _WORKER_H_
#define _WORKER_H_

#include <iostream>
#include <random>
#include <unordered_set>

#include <unistd.h>

#include "types.hpp"
#include "config.hpp"
#include "logBuffer.hpp"

intptr_t genRandPtr() {
    std::random_device rd;
    std::uniform_int_distribution<uintptr_t> dist(0,UINTPTR_MAX);
    return dist(rd);
}

extern thread_local unsigned node_id;
class Worker {
    std::unordered_set<uintptr_t> to_flush;
    LogBuffer *my_buffer;
    Log *curr_log;

public:
    Worker(LogBuffer *buf): my_buffer(buf), curr_log(buf->getHead()) {}

    void run() {
        unsigned count = 0;
        while(count < EPOCH) {
                uintptr_t addr = genRandPtr();
                uintptr_t cl_addr = addr & CACHELINEMASK;
                if(curr_log->isFull()) {
                    count++;
                    epoch_t head = my_buffer->moveHead();
                    curr_log = my_buffer->getHead();
                    
                    std::cout << "node " << node_id << ": produce log " << head << std::endl;
    }
                while(!curr_log->write(cl_addr))
                    //wait for copiers to finish
                    sleep(1);
        }
    }
};


#endif
