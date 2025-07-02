#ifndef _USER_H_
#define _USER_H_

#include <iostream>
#include <random>
#include <unordered_set>

#include <unistd.h>

#include "flush.hpp"
#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

enum UserOp {
    OP_STORE,
    OP_LOAD,
    OP_END
};

static inline size_t genRandOffset(size_t range, size_t align) {
    std::random_device rd;
    std::uniform_int_distribution<size_t> dist(0, range-1);
    return (dist(rd)/align) * align;
}

static inline UserOp genRandOp() {
    int num = rand() % 100;
    return num > 50? OP_STORE: OP_LOAD;
}

static inline size_t genSeqOffset(size_t range, size_t align, size_t index) {
    return (align * index) %range;
}

static inline UserOp genSeqOp(size_t index) {
    return index%2 == 0? OP_STORE: OP_LOAD;
}

class User {
    //user local data
    unsigned node_id;
    unsigned user_id;
    std::unordered_set<virt_addr_t> dirty_cls;
    Log *curr_log;
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
    unsigned blocked_count = 0;

    inline LogBuffer &my_buf() {
        return cxl_meta.bufs[node_id];
    }

public:
    User(unsigned nid, unsigned uid, CXLPool &pool, NodeLocalMeta &local_meta): node_id(nid), user_id(uid),  cxl_meta(pool.meta), dirty_cls(LOG_SIZE), cxl_data(pool.data), cache_info(local_meta.cache_info), user_clock(local_meta.user_clock) {}

    //should support taking multiple heads for more flexibility
    void write_to_log(bool is_release) {
        Log *curr_log;
        while(!(curr_log = my_buf().take_tail())) {
#ifdef STATS
            blocked_count++;
#endif
            //wait for available space
            sleep(0);
        }

        for(auto cl: dirty_cls) {
            curr_log->write(cl);
            do_flush((char *)(cl));
        }
        flush_fence();
        curr_log->produce(is_release); 
        dirty_cls.clear();
    }

    void handle_store(char *addr, bool is_release) {
        virt_addr_t cl_addr = (virt_addr_t)addr & CACHE_LINE_MASK;

        dirty_cls.insert(cl_addr);
        //write to log either on release store or on reaching log size limit
        if(is_release || dirty_cls.size() == LOG_SIZE) {
            write_to_log(is_release);
            LOG_INFO("node " << node_id << " produce log " << cache_info.produced_count++);
        }

        if(is_release) {
           const auto &clock = user_clock.mod([=] (auto &self) {
               self.tick(node_id);
               return self;
           });
           LOG_INFO("node " << node_id << " release at " << (void *)addr << std::dec << ", clock=" << clock);
           size_t off = addr - cxl_data;
           cxl_meta.atmap[off].mod([&](auto &self) {
               self.clock.merge(clock);
           });
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
#ifdef STATS
            invalidate_count++;
#endif
        }

        char ret;
        if (is_acquire) {
            size_t off = addr - cxl_data;
            auto at_clk = cxl_meta.atmap[off].get([&](auto &self) { 
                ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
                return self.clock; 
            });

            LOG_INFO("node " << node_id << " acquire " << (void*) addr << std::dec << ", clock=" << at_clk);

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
#ifdef STATS
        invalidate_count++;
#endif
        if (is_acquire)
            ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
        else
            ret = *((volatile char *)addr);
        return ret;

    };

    void run() {
        for (int i =0; i < TOTAL_OPS; i++) {
#ifdef SEQ_WORKLOAD
            bool is_atomic = (i % ATOMIC_PLAIN_RATIO == 0);
            UserOp user_op = genSeqOp(i);
            size_t off = genSeqOffset(is_atomic ? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE, 8, i);
#else
            // random version is not data-race free
            bool is_atomic = (rand() % ATOMIC_PLAIN_RATIO == 0);
            UserOp user_op = genRandOp();
            size_t off = genRandOffset(is_atomic? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE, 8);
#endif
            //TODO: data-race-free workload based on synchronization (locked region?)
            switch (user_op) {
                case OP_STORE: {
#ifdef STATS
                    write_count++;
#endif
#ifndef PROTOCOL_OFF
                    handle_store(&cxl_data[off], is_atomic);
#else
                    handle_store_raw(&cxl_data[off], is_atomic);
#endif
                    break;
                } 
                case OP_LOAD: {
#ifdef STATS
                    read_count++;
#endif
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
