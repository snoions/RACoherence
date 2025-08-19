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
    VectorClock thread_clock;
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
    User(CXLPool &pool, NodeLocalMeta &local_meta): cxl_meta(pool.meta), cxl_data(pool.data), cache_info(local_meta.cache_info) {}

    inline char handle_load(char *addr, bool is_acquire = false) {
            if (cache_info.invalidate_if_dirty(addr)) {
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

            LOG_INFO("node " << node_id << " acquire " << (void*) addr << std::dec << ", clock=" << at_clk)

#ifdef USER_HELP_CONSUME
            user_help_consume(at_clk);
#else
            wait_for_consume(at_clk);
#endif

        } else
            ret = *((volatile char *)addr);
        return ret;
    }


    inline void handle_store(char *addr, bool is_release = false) {
        uintptr_t cl_addr = (uintptr_t)addr & CACHE_LINE_MASK;

        while (dirty_cls.insert(cl_addr) || dirty_cls.get_length_entry_count() != 0)
            write_to_log(false);

        if(is_release) {
#ifdef LOCAL_CL_TABLE_BUFFER
            while (dirty_cls.dump_buffer_to_table())
                write_to_log(false);
#endif

            clock_t clk_val = write_to_log(true);
            thread_clock.merge(node_id, clk_val);
            LOG_INFO("node " << node_id << " release at " << (void *)addr << std::dec << ", thread clock=" <<thread_clock)
            size_t off = addr - cxl_data;
#ifdef LOCATION_CLOCK_MERGE
            cxl_meta.atmap[off].mod([&](auto &self) {
                self.clock.merge(thread_clock);
            });
            ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
#else
            cxl_meta.atmap[off].mod([&](auto &self) {
                self.clock = thread_clock;
                ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
            });
#endif
        } else {
            if (cache_info.invalidate_if_dirty(addr)) {
#ifdef STATS
                invalidate_count++;
#endif
            }
            *((volatile char *)addr) = 0;
        }
    }

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
