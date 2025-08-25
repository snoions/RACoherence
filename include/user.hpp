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

    //thread local data
    VectorClock thread_clock;
    LocalCLTable dirty_cls;

    //user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;
    unsigned blocked_count = 0;

public:
    User(CXLPool &pool, NodeLocalMeta &local_meta): cxl_meta(pool.meta), cxl_data(pool.data), cache_info(local_meta.cache_info) {}

    inline LogManager &my_buf() { return cxl_meta.bufs[node_id]; }

    clock_t write_to_log(bool is_release);

    void help_consume(const VectorClock &target);

    void wait_for_consume(const VectorClock &target);

    inline void handle_invalidate(char *addr) {
#ifndef EAGER_INVALIDATE
            if (cache_info.invalid_cls.invalidate_if_dirty((uintptr_t)addr)) {
#ifdef STATS
                invalidate_count++;
#endif
            }
#endif
    }

    inline void handle_invalidate_raw(char *addr) {
        do_invalidate(addr);
        invalidate_fence();
#ifdef STATS
        invalidate_count++;
#endif
    }

    inline const VectorClock &thread_release() {
#ifdef LOCAL_CL_TABLE_BUFFER
        while (dirty_cls.dump_buffer_to_table())
            write_to_log(false);
#endif
        clock_t clk_val = write_to_log(true);
        thread_clock.merge(node_id, clk_val);
        return thread_clock;
    }

    inline void thread_acquire(const VectorClock &clock) {
        thread_clock.merge(clock);
#ifdef USER_HELP_CONSUME
        help_consume(clock);
#else
        wait_for_consume(clock);
#endif
    }

    inline void handle_store_release(char *addr, char val) {
        auto thread_clock = thread_release();
        LOG_DEBUG("node " << node_id << " release at " << (void *)addr << std::dec << ", thread clock=" <<thread_clock)
        size_t off = addr - cxl_data;
#ifdef LOCATION_CLOCK_MERGE
        cxl_meta.atmap[off].mod([&](auto &self) {
            self.clock.merge(thread_clock);
        });
        ((volatile std::atomic<char> *)addr)->store(val, std::memory_order_release);
#else
        cxl_meta.atmap[off].mod([&](auto &self) {
            self.clock = thread_clock;
            ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
        });
#endif
    }

    inline void handle_store(char *addr, char val) {
        uintptr_t cl_addr = (uintptr_t)addr & CACHE_LINE_MASK;

        while (dirty_cls.insert(cl_addr) || dirty_cls.get_length_entry_count() != 0)
            write_to_log(false);

        handle_invalidate(addr);
        *((volatile char *)addr) = val;
    }

    inline char handle_load_acquire(char *addr) {
        char ret;
        size_t off = addr - cxl_data;
        auto at_clk = cxl_meta.atmap[off].get([&](auto &self) {
            ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
            return self.clock;
        });

        LOG_DEBUG("node " << node_id << " acquire " << (void*) addr << std::dec << ", clock=" << at_clk)

        thread_acquire(at_clk);
        return ret;
    }

    inline char handle_load(char *addr) {
        handle_invalidate(addr);
        return *((volatile char *)addr);
    }

    inline void handle_store_release_raw(char *addr, char val) {
        ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
        flush_fence();
    };

    inline void handle_store_raw(char *addr, char val) {
        handle_invalidate_raw(addr);
        *((volatile char *)addr) = val;
        do_flush((char *)addr);
    }

    inline char handle_load_acquire_raw(char *addr) {
        return ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
    };

    inline char handle_load_raw(char *addr) {
        handle_invalidate_raw(addr);
        return *((volatile char *)addr);
    }

    template <typename W>
    void run(W &workload);
};

#endif
