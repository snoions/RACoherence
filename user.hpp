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
#include "logger.hpp"
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
    CacheInfo *cache_info;
    VectorClock thread_clock;
    unsigned count = 0;

    static Op genRandOp() {
        int num = rand() % 100;
        if (node_id == 0)
            return num > 80? OP_STORE: OP_LOAD;
        return num > 20? OP_STORE: OP_LOAD;
    };

public:
    User(LogBuffer *buf, CacheInfo *cinfo, ALocMap *alc): node_buffer(buf), cache_info(cinfo), alocs(alc) {}

    //should support taking multiple heads for more flexibility
    Log *write_to_log() {
        Log *curr_log;
        while(!(curr_log = node_buffer->takeHead()))
            //wait for copiers to finish
            sleep(0);
        
        for(auto cl: dirty_cls)
            curr_log->write(cl);
        return curr_log;
    }

    void handle_store(uintptr_t addr, bool is_release) {
        uintptr_t cl_addr = addr & CACHELINEMASK;
        dirty_cls.insert(cl_addr);
        if(is_release || dirty_cls.size() == LOGSIZE) {
            Log *curr_log = write_to_log();                    
            thread_clock.tick(node_id);
            curr_log->publish(is_release); 
            dirty_cls.clear();
            std::stringstream ss;
            if(is_release) {
                ss << " release at " << std::hex << addr << " " << std::dec;
                alocs->at(addr).mod([&](auto &self) {
                    self.log = curr_log;
                    self.clock.merge(thread_clock);
                });
            }
            LOG_DEBUG("node " << node_id << ss.str() << "produce log " << count++);
        }
    }
    
    bool check_clock_add_task(const VectorClock &aloc_clk, const VectorClock &cclk) {
        bool uptodate = false;
        std::vector<CacheInfo::Task> tq;
        for (unsigned i=0; i<NODECOUNT; i++)
            if (i != node_id && cclk[i] < aloc_clk[i]) {
                tq.push_back({i, aloc_clk[i]});
                uptodate = false;
            }

        if (!tq.empty())
            cache_info->task_queue.mod([&](auto &self) { 
                for (auto t: tq)
                    self->push(t); 
            });
        return uptodate;
    }


    void handle_load(uintptr_t addr, bool is_acquire) {
        if (is_acquire) {
            LOG_DEBUG("node " << node_id << " acquire " << std::hex << addr << std::dec);
            //value should also be loaded at this point
            auto &aloc_clk = alocs->at(addr).get([&](auto &self) { return self.clock; });
            auto &cclk = cache_info->clock.get([](auto &self) {return self; });
            
            bool uptodate = check_clock_add_task(aloc_clk, cclk);
            //bool uptodate = false;
                        
            for (; !uptodate; uptodate = true) {
                sleep(0);
                LOG_DEBUG("block" << uptodate);
                auto &cclk = cache_info->clock.get([](auto &self) {return self; });
                for (unsigned i=0; i<NODECOUNT; i++)
                    if (i != node_id && cclk[i] < aloc_clk[i])
                        uptodate = false;
            }

            thread_clock.merge(aloc_clk);
        }
    }

    void run() {
        while(count < EPOCH) {
            uintptr_t addr = genRandPtr();
            Op user_op = genRandOp();
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
