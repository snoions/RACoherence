#ifndef _USER_H_
#define _USER_H_

#include <iostream>
#include <sstream>
#include <random>
#include <unordered_set>
#include <map>

#include <unistd.h>

#include "cacheops.hpp"
#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

static size_t genRandOffset(size_t range) {
    std::random_device rd;
    std::uniform_int_distribution<size_t> dist(0, range-1);
    return (dist(rd)/8) * 8;
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
    CXLMemMeta &cxl_meta;
    char *cxl_data;
    //node local data
    CacheInfo &cache_info;
    Monitor<VectorClock> &user_clock;

    static Op genRandOp() {
        int num = rand() % 100;
        return num > 50? OP_STORE: OP_LOAD;
    };

public:
    User(CXLMemMeta &meta, char *data, CacheInfo &cinfo, Monitor<VectorClock> &uclk): cxl_meta(meta), cxl_data(data), cache_info(cinfo), user_clock(uclk) {}

    //should support taking multiple heads for more flexibility
    Log *write_to_log(bool is_release) {
        Log *curr_log;
        while(!(curr_log = cxl_meta.bufs[node_id].take_head()))
            //wait for available space
            sleep(0);
        
        for(auto cl: dirty_cls) {
            curr_log->write(cl);
            do_flush((char *)cl); 
        }
        flush_fence();
        curr_log->publish(is_release); 
        dirty_cls.clear();
        return curr_log;
    }

    void handle_store(char *addr, bool is_release) {
        if (is_release)
            ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
        else
            *((volatile char *)addr) = 0;

        virt_addr_t cl_addr = (virt_addr_t)addr & CACHE_LINE_MASK;
        dirty_cls.insert(cl_addr);
        //write to log either on release store or on reaching log size limit
        if(is_release || dirty_cls.size() == LOG_SIZE) {
            Log *curr_log = write_to_log(is_release);                    

            const auto &clock = user_clock.mod([] (auto &self) {
                self.tick(node_id);
                return self;
            });

            std::stringstream ss;
            if(is_release) {
                ss << " release at " << std::hex << addr << std::dec << ", clock=" << clock << " ";
                size_t off = addr - cxl_data;
                cxl_meta.atmap[off].mod([&](auto &self) {
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

            std::unique_lock<std::mutex> l(cxl_meta.bufs[i].get_tail_mutex(node_id));
            //check again after wake up
            val = cache_info.get_clock(i);

            while(val < target[i]) {
                //must not be NULL
                Log *tail = cxl_meta.bufs[i].take_tail(node_id);
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
                LOG_DEBUG("block on acquire " << c <<" target=" << at_clk << ", current=" << cclk );
            sleep(0);
        }
    }

    char handle_load(char *addr, bool is_acquire) {
        if (cache_info.tracker.is_dirty((virt_addr_t)addr)) {
            do_invalidate(addr);
            invalidate_fence();
        }

        char ret;
        if (is_acquire) {
            size_t off = addr - cxl_data;
            auto at_clk = cxl_meta.atmap[off].get([&](auto &self) { 
                ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
                return self.clock; 
            });

            LOG_DEBUG("node " << node_id << " acquire at " << std::hex << addr << std::dec << ", target=" << at_clk);

            //task queue not useful for now, may be for skewed patterns?
            //wait_for_cache_clock(at_clk);
            catch_up_cache_clock(at_clk);

            user_clock.mod([&](auto &self) {
                self.merge(at_clk);
            });
        } else 
            ret = *((volatile char *)addr);

        return ret;
    }

    void run() {
        while(count < EPOCH) {
            bool is_atomic = (rand() % ATOMIC_PLAIN_RATIO == 0);
            size_t off = genRandOffset(is_atomic? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE);
            Op user_op = genRandOp();
            switch (user_op) {
                case OP_STORE: {
                    handle_store(&cxl_data[off], is_atomic);
                    break;
                } 
                case OP_LOAD: {
                    handle_load(&cxl_data[off], is_atomic);
                    break;
                }
                default:
                    assert("unreachable");
            }
        }
    }
};


#endif
