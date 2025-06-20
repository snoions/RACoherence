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

static virt_addr_t genRandPtr() {
    std::random_device rd;
    std::uniform_int_distribution<virt_addr_t> dist(0, CXLMEM_RANGE);
    return dist(rd);
}

extern thread_local unsigned node_id;
class User {
    enum Op {
        OP_STORE, 
        OP_LOAD,
        OP_END
    };
    
    //user local data
    std::unordered_set<virt_addr_t> dirty_cls;
    unsigned count = 0;
    //CXL mem shared data
    ALocMap &alocs; 
    PerNode<LogBuffer> &bufs;
    //node local data
    CacheInfo &cache_info;
    Monitor<VectorClock> &user_clock;

    static Op genRandOp() {
        int num = rand() % 100;
        return num > 50? OP_STORE: OP_LOAD;
    };

public:
    User(PerNode<LogBuffer> &b, ALocMap &alc, CacheInfo &cinfo, Monitor<VectorClock> &uclk): bufs(b), alocs(alc), cache_info(cinfo), user_clock(uclk) {}

    //should support taking multiple heads for more flexibility
    Log *write_to_log() {
        Log *curr_log;
        while(!(curr_log = bufs[node_id].take_head()))
            //wait for available space
            sleep(0);
        
        for(auto cl: dirty_cls)
            curr_log->write(cl);
        return curr_log;
    }

    void handle_store(virt_addr_t addr, bool is_release) {
        virt_addr_t cl_addr = addr & CACHE_LINE_MASK;
        if (dirty_cls.insert(cl_addr).second) {}
            //TODO: flush
        //write to log either on release store or on reaching log size limit
        if(is_release || dirty_cls.size() == LOG_SIZE) {
            Log *curr_log = write_to_log();                    
            curr_log->publish(is_release); 
            dirty_cls.clear();
            const auto &clock = user_clock.mod([] (auto &self) {
                self.tick(node_id);
                return self;
            });

            std::stringstream ss;
            if(is_release) {
                ss << " release at " << std::hex << addr << std::dec << ", clock=" << clock << " ";
                alocs.at(addr).mod([&](auto &self) {
                    self.log = curr_log;
                    self.clock.merge(clock);
                });
            }

            LOG_INFO("node " << node_id << ss.str() << "produce log " << count);
            count++;
        }
    }

    void catch_up_cache_clock(const VectorClock &target) {
        for (unsigned i=0; i<NODE_COUNT; i++) {
            if (i == node_id)
                continue;
            auto val = cache_info.get_clock(i);
            if (val >= target[i])
                continue;

            std::unique_lock<std::mutex> l(bufs[i].get_tail_mutex(node_id));
            //check again after wake up
            val = cache_info.get_clock(i);

            while(val < target[i]) {
                Log *tail;
                //TODO: optimize
                while(!(tail = bufs[i].take_tail(node_id)));
                cache_info.process_log(tail);
                if (tail->is_release())
                    val = cache_info.update_clock(i);
                tail->consume();
                LOG_INFO("node " << node_id << " consume log " << cache_info.consumed_count << " of " << i);
                cache_info.consumed_count++;
            }

        }
    }

    void wait_for_cache_clock(const VectorClock &target) {
        int c = 0;       
        while(true) {
            bool uptodate = true;
            for (unsigned i=0; i<NODE_COUNT; i++)
                if (i != node_id && cache_info.get_clock(i) < target[i]) {
                    uptodate = false;
                    break;
                }
            if (uptodate)
                break;
            if (c++>0)
                LOG_DEBUG("block on acquire " << c <<" target=" << aloc_clk << ", current=" << cclk );
            sleep(0);
        }
    }

    void handle_load(virt_addr_t addr, bool is_acquire) {
        if (cache_info.tracker.is_dirty(addr)) {}
            //TODO: flush
        if (is_acquire) {
            //value should also be loaded at this point
            auto aloc_clk = alocs.at(addr).get([&](auto &self) { return self.clock; });
            LOG_DEBUG("node " << node_id << " acquire at " << std::hex << addr << std::dec << ", target=" << aloc_clk);

            //task queue not useful for now, may be for skewed patterns?
            //wait_for_cache_clock(aloc_clk);
            catch_up_cache_clock(aloc_clk);

            user_clock.mod([&](auto &self) {
                self.merge(aloc_clk);
            });
        }
    }

    void run() {
        while(count < EPOCH) {
            virt_addr_t addr = genRandPtr();
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
