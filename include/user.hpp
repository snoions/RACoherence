#ifndef _USER_H_
#define _USER_H_

#include <unistd.h>

#include "flushUtils.hpp"
#include "config.hpp"
#include "logManager.hpp"
#include "memLayout.hpp"
#include "localCLTable.hpp"

extern thread_local unsigned node_id;
extern thread_local unsigned user_id;

class User {
    //CXL mem shared data
    CXLMemMeta &cxl_meta;
    char *cxl_data;

    //node local data
    CacheInfo &cache_info;
    Monitor<VectorClock> &user_clock;
    LocalCLTable dirty_cls;
    Log *curr_log;

    //user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;
    unsigned blocked_count = 0;

    inline LogManager &my_buf() { return cxl_meta.bufs[node_id]; }

    clock_t write_to_log(bool is_release);

    void user_help_consume(const VectorClock &target);

    void wait_for_consume(const VectorClock &target);

public:
    User(CXLPool &pool, NodeLocalMeta &local_meta): cxl_meta(pool.meta), cxl_data(pool.data), cache_info(local_meta.cache_info), user_clock(local_meta.user_clock) {}

    void handle_store(char *addr, bool is_release = false);

    char handle_load(char *addr, bool is_acquire = false);

    inline void handle_store_raw(char *addr, bool is_release = false) {
        //assuming we don't know which writes overwrite entire cache lines
        do_invalidate(addr);
        invalidate_fence();
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

    template <typename W>
    void run(W &workload);
};

#endif
