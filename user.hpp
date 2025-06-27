#ifndef _USER_H_
#define _USER_H_

#include <iostream>
#include <sstream>
#include <random>
#include <unordered_set>

#include <unistd.h>

#include "flush.hpp"
#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

extern thread_local unsigned node_id;

static size_t genRandOffset(size_t range, size_t align) {
    std::random_device rd;
    std::uniform_int_distribution<size_t> dist(0, range-1);
    return (dist(rd)/align) * align;
}

struct SeqOffsetGen {
    size_t range;
    size_t stride;
    size_t curr;

    SeqOffsetGen(size_t r, size_t s): range(r), stride(s), curr(0) {};

    size_t gen() {
        auto old = curr;
        curr = (curr+1)%range;
        return curr;
    }
};

class User {
    enum Op {
        OP_STORE, 
        OP_LOAD,
        OP_END
    };
    
    //user local data
    //TODO: change to a cache indexed by last few bytes of address, and flush upon eviction, like in Atlas
    unsigned node_id;
    unsigned user_id;
    std::unordered_set<virt_addr_t> dirty_cls;
    //CXL mem shared data
    CXLMemMeta &cxl_meta;
    char *cxl_data;
    //node local data
    CacheInfo &cache_info;
    Monitor<VectorClock> &user_clock;
    //user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;

    static Op genRandOp() {
        int num = rand() % 100;
        return num > 50? OP_STORE: OP_LOAD;
    };

public:
    User(unsigned nid, unsigned uid, CXLPool &pool, NodeLocalMeta &local_meta): cxl_meta(pool.meta), node_id(nid), user_id(uid), cxl_data(pool.data), cache_info(local_meta.cache_info), user_clock(local_meta.user_clock) {}

    //should support taking multiple heads for more flexibility
    Log *write_to_log(bool is_release) {
        Log *curr_log;
        while(!(curr_log = cxl_meta.bufs[node_id].take_tail()))
            //wait for available space
            sleep(0);
        
        for(auto cl: dirty_cls) {
            curr_log->write(cl);
            do_flush((char *)cl); 
        }
        flush_fence();
        curr_log->produce(is_release); 
        dirty_cls.clear();
        return curr_log;
    }

    void handle_store(char *addr, bool is_release) {
        virt_addr_t cl_addr = (virt_addr_t)addr & CACHE_LINE_MASK;
        
        dirty_cls.insert(cl_addr);
        //write to log either on release store or on reaching log size limit
        if(is_release || dirty_cls.size() == LOG_SIZE) {
            Log *curr_log = write_to_log(is_release);                    
            std::stringstream ss;
            if(is_release) {
                const auto &clock = user_clock.mod([=] (auto &self) {
                    self.tick(node_id);
                    return self;
                });
                ss << " release at " << (void *)addr << ", clock=" << clock << " ";
                size_t off = addr - cxl_data;
                cxl_meta.atmap[off].mod([&](auto &self) {
                    self.clock.merge(clock);
                });
            }

            LOG_INFO("node " << node_id << ss.str() << "produce log " << cache_info.produced_count++);
        }

        if (is_release)
            ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
        else
            *((volatile char *)addr) = 0;
    }

    void catch_up_cache_clock(const VectorClock &target) {
        for (unsigned i=0; i<NODE_COUNT; i++) {
            if (i == node_id)
                continue;
            auto val = cache_info.get_clock(i);
            if (val >= target[i])
                continue;

            std::unique_lock<std::mutex> l(cxl_meta.bufs[i].get_head_mutex(node_id));
            //check again after wake up
            val = cache_info.get_clock(i);

            while(val < target[i]) {
                //must not be NULL
                Log &log = cxl_meta.bufs[i].take_head(node_id);
                cache_info.process_log(log);
                if (log.is_release())
                    val = cache_info.update_clock(i);
                log.consume();
                LOG_INFO("node " << node_id << " consume log " << ++cache_info.consumed_count << " from " << i);
            }

        }
    }

    void wait_for_cache_clock(const VectorClock &target) {
        while(true) {
            bool uptodate = true;
            for (unsigned i=0; i<NODE_COUNT; i++) {
                auto curr = cache_info.get_clock(i);
                if (i != node_id && curr < target[i]) {
                    uptodate = false;
                    LOG_DEBUG("block on acquire, index=" << i << ", target=" << target[i] << ", current=" << curr);
                    break;
                }
            }
            if (uptodate)
                break;
            sleep(0);
        }
    }

    char handle_load(char *addr, bool is_acquire) {
        if (cache_info.is_dirty(addr)) {
            do_invalidate(addr);
            invalidate_fence();
            invalidate_count++;
        }

        char ret;
        if (is_acquire) {
            size_t off = addr - cxl_data;
            auto at_clk = cxl_meta.atmap[off].get([&](auto &self) { 
                ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
                return self.clock; 
            });

            LOG_INFO("node " << node_id << " acquire " << (void*) addr << ", clock=" << at_clk);

#ifdef USER_CONSUME_LOGS
            catch_up_cache_clock(at_clk);
#else
            wait_for_cache_clock(at_clk);
#endif

            user_clock.mod([&](auto &self) {
                self.merge(at_clk);
            });
        } else 
            ret = *((volatile char *)addr);

        return ret;
    }

    inline void handle_store_raw(char *addr, bool is_release) {
        if (is_release) {
            ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
            flush_fence();
        } else {
            *((volatile char *)addr) = 0;
            do_flush((char *)addr);
        }
    };

    inline char handle_load_raw(char *addr, bool is_acquire) {
        char ret;
        do_invalidate(addr);
        invalidate_fence();
        invalidate_count++;
        if (is_acquire)
            ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
        else
            ret = *((volatile char *)addr);
        return ret;

    };

    void run() {
        SeqOffsetGen plain_gen(CXLMEM_RANGE, 8);
        SeqOffsetGen atomic_gen(CXLMEM_ATOMIC_RANGE, 8);

        while(write_count < TOTAL_WRITES) {
            bool is_atomic = (rand() % ATOMIC_PLAIN_RATIO == 0);
#ifdef SEQ_WORKLOAD
            size_t off = is_atomic? atomic_gen.gen(): plain_gen.gen();
#else
            size_t off = genRandOffset(is_atomic? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE, 8);
#endif
            Op user_op = genRandOp();
            switch (user_op) {
                case OP_STORE: {
                    write_count++;
#ifndef PROTOCOL_OFF
                    handle_store(&cxl_data[off], is_atomic);
#else
                    handle_store_raw(&cxl_data[off], is_atomic);
#endif
                    break;
                } 
                case OP_LOAD: {
                    read_count++;
#ifndef PROTOCOL_OFF
                    handle_load(&cxl_data[off], is_atomic);
#else
                    handle_load_raw(&cxl_data[off], is_atomic);
#endif
                    break;
                }
                default:
                    assert("unreachable");
            }
        }
    }
};


#endif
