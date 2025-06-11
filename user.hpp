#ifndef _USER_H_
#define _USER_H_

#include <iostream>
#include <random>
#include <unordered_set>

#include <unistd.h>

#include "config.hpp"
#include "logBuffer.hpp"

static intptr_t genRandPtr() {
    std::random_device rd;
    std::uniform_int_distribution<uintptr_t> dist(0,UINTPTR_MAX);
    return dist(rd);
}

extern thread_local unsigned node_id;
class User {
    std::unordered_set<uintptr_t> dirty_cls;
    LogBuffer *my_buffer;
    unsigned count;

public:
    User(LogBuffer *buf): my_buffer(buf) {}

    //TODO: support taking multiple heads
    void write_to_log() {
        Log *curr_log;
        while(!(curr_log = my_buffer->takeHead()))
            //wait for copiers to finish
            sleep(0);
        
        for(auto cl: dirty_cls)
            curr_log->write(cl);
        curr_log->publish();
        
    }

    void handle_store(uintptr_t addr, bool is_release) {
        uintptr_t cl_addr = addr & CACHELINEMASK;
        dirty_cls.insert(cl_addr);
        if(is_release || dirty_cls.size() == LOGSIZE) {
            write_to_log();                    
            std::cout << "node " << node_id << ": produce log " << count++ << std::endl;
        }
    }

    void handle_load(bool is_acquire) {
        //TODO
    }

    void run() {
        while(count < EPOCH) {
            uintptr_t addr = genRandPtr();
            bool is_release = (rand() % 100) == 0;
            handle_store(addr, is_release);
        }
    }
};


#endif
