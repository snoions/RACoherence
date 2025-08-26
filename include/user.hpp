#ifndef _USER_H_
#define _USER_H_

#include "flushUtils.hpp"
#include "config.hpp"
#include "logManager.hpp"
#include "localCLTable.hpp"
#include "threadOps.hpp"
#include "vectorClock.hpp"

extern thread_local ThreadOps *thread_ops;

// Should be power of two
constexpr uintptr_t CXL_NHC_RANGE = 1ull << 30;
constexpr uintptr_t CXL_HC_RANGE = 1ull << 20;
constexpr uintptr_t CXL_SYNC_DEVICE_COUNT = 1ull << 4;

struct AtomicMeta {
    VectorClock clock;

    AtomicMeta(): clock() {};
};

struct CXLMemMeta {
    //PerNode<LogManager> bufs;

    //TODO: support dynamically allocated atomic locs
    Monitor<AtomicMeta> atmap[CXL_SYNC_DEVICE_COUNT];

    //CXLMemMeta(): bufs(), atmap{} {};
    CXLMemMeta(): atmap{} {};
};

// CXLPool should be in CXL-NHC memory
struct CXLPool {
    CXLMemMeta meta;
    //alignas(CACHE_LINE_SIZE) CXLMutex mutexes[CXL_SYNC_DEVICE_COUNT];
    //alignas(CACHE_LINE_SIZE) CXLAtomic<char> atomic_data[CXL_SYNC_DEVICE_COUNT];
    alignas(CACHE_LINE_SIZE) char data[CXL_NHC_RANGE];

    CXLPool(): meta() {};
    //CXLPool(): atomic_data{} {};
};

class User {
    // CXL mem shared data
    CXLPool &cxl_pool;

    unsigned node_id;
    unsigned user_id;
    // user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;
    unsigned blocked_count = 0;

public:
    User(CXLPool &pool, unsigned nid, unsigned uid): cxl_pool(pool), node_id(nid), user_id(uid) {}

    inline void handle_invalidate_raw(char *addr) {
        do_invalidate(addr);
        invalidate_fence();
#ifdef STATS
        invalidate_count++;
#endif
    }

    inline void handle_store_release(char *addr, char val) {
        auto thread_clock = thread_ops->thread_release();
        LOG_DEBUG("node " << node_id << " release at " << (void *)addr << std::dec << ", thread clock=" <<thread_clock)
        size_t off = addr - cxl_pool.data;
#ifdef LOCATION_CLOCK_MERGE
        cxl_pool.meta.atmap[off].mod([&](auto &self) {
            self.clock.merge(thread_clock);
        });
        ((volatile std::atomic<char> *)addr)->store(val, std::memory_order_release);
#else
        cxl_pool.meta.atmap[off].mod([&](auto &self) {
            self.clock = thread_clock;
            ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
        });
#endif
    }

    inline void handle_store(char *addr, char val) {
        if (thread_ops->check_invalidate(addr)) {
#ifdef STATS
            invalidate_count++;
#endif
        }
        thread_ops->log_store(addr);
        *((volatile char *)addr) = val;
    }

    inline char handle_load_acquire(char *addr) {
        char ret;
        size_t off = addr - cxl_pool.data;
        auto at_clk = cxl_pool.meta.atmap[off].get([&](auto &self) {
            ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
            return self.clock;
        });

        LOG_DEBUG("node " << node_id << " acquire " << (void*) addr << std::dec << ", clock=" << at_clk)

        thread_ops->thread_acquire(at_clk);
        return ret;
    }

    inline char handle_load(char *addr) {
        if (thread_ops->check_invalidate(addr)) {
#ifdef STATS
            invalidate_count++;
#endif
        }
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
