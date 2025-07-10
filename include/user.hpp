#ifndef _USER_H_
#define _USER_H_

#include <iostream>
#include <random>
#include <unordered_set>

#include <unistd.h>

#include "flush.hpp"
#include "config.hpp"
#include "localCLTracker.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"
#include "workload.hpp"

class User {
    //user local data
    unsigned node_id;
    unsigned user_id;
    LocalCLTracker dirty_cls;
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

    inline LogBuffer &my_buf() { return cxl_meta.bufs[node_id]; }

    void write_to_log(bool is_release);

    void catch_up_cache_clock(const VectorClock &target);

    void wait_for_cache_clock(const VectorClock &target);

public:
    User(unsigned nid, unsigned uid, CXLPool &pool, NodeLocalMeta &local_meta): node_id(nid), user_id(uid),  cxl_meta(pool.meta), cxl_data(pool.data), cache_info(local_meta.cache_info), user_clock(local_meta.user_clock) {}


    void handle_store(char *addr, bool is_release = false);

    char handle_load(char *addr, bool is_acquire = false);

    inline void handle_store_raw(char *addr, bool is_release = false) {
        if (is_release) {
            ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
            flush_fence();
        } else {
            *((volatile char *)addr) = 0;
            do_flush((char *)addr);
        }
    };

    inline char handle_load_raw(char *addr, bool is_acquire = false) {
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

    void run();
};

#endif
