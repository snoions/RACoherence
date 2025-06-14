#ifndef _USER_H_
#define _USER_H_

#include <iostream>
#include <sstream>
#include <random>
#include <unordered_set>
#include <map>

#include <unistd.h>

#include "config.hpp"
#include "logBuffer.hpp"
#include "memLayout.hpp"

static intptr_t genRandPtr() {
    std::random_device rd;
    std::uniform_int_distribution<uintptr_t> dist(0, CXLMEM_RANGE);
    return dist(rd);
}

extern thread_local unsigned node_id;
class User {
    enum Op {
        OP_STORE, 
        OP_LOAD,
        OP_END
    };

    std::unordered_set<uintptr_t> dirty_cls;
    ALocMap *alocs; 
    LogBuffer *node_buffer;
    Monitor<VectorClock> *cache_clock;
    VectorClock thread_clock;
    unsigned count = 0;

public:
    User(LogBuffer *buf, Monitor<VectorClock> *clk, ALocMap *alc): node_buffer(buf), cache_clock(clk), alocs(alc) {}

    //should support taking multiple heads for more flexibility
    Log *write_to_log() {
        Log *curr_log;
        while(!(curr_log = node_buffer->takeHead()))
            //wait for copiers to finish
            sleep(0);
        
        for(auto cl: dirty_cls)
            curr_log->write(cl);
        thread_clock.tick(node_id);
        curr_log->publish(); 
        return curr_log;
    }

    void handle_store(uintptr_t addr, bool is_release) {
        uintptr_t cl_addr = addr & CACHELINEMASK;
        dirty_cls.insert(cl_addr);
        if(is_release || dirty_cls.size() == LOGSIZE) {
            Log *curr_log = write_to_log();                    
            dirty_cls.clear();
            std::stringstream ss;
            ss << "node " << node_id << ": " << std::endl;
            if(is_release) {
                ss << "release at " << std::hex << addr << std::endl;
                alocs->at(addr).mod([&](auto &self) {
                    self.setLog(curr_log);
                    self.setClock(thread_clock);
                });
            }
            std::cout << ss.str() << "produce log " << std::dec << count++ << std::endl;
        }
    }

    void handle_load(uintptr_t addr, bool is_acquire) {
        if (is_acquire) {
            std::cout << "node " << node_id << ": acquire " << std::hex << addr << std::endl;
            bool loop = true;
            while (!loop) {
                loop = alocs->at(addr).get([&](auto &self) {
                    auto &cclk = cache_clock->get([](auto &vc) {return vc; });
                    return self.clock.le_at(cclk, node_id); 
                });
            }
        }
    }

    void run() {
        while(count < EPOCH) {
            uintptr_t addr = genRandPtr();
            Op user_op = (Op) (rand() % OP_END);
            switch (user_op) {
                case OP_STORE: {
                    bool is_release = isAtomic(addr);
                    handle_store(addr, is_release);
                    break;
                } 
                case OP_LOAD: {
                    bool is_acquire = isAtomic(addr);
                    handle_load(addr, is_acquire);
                    break;
                }
                default:
                    assert("unreachable");
            }
        }
    }
};


#endif
