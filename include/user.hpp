#ifndef _USER_H_
#define _USER_H_

#include "flushUtils.hpp"
#include "config.hpp"
#include "threadOps.hpp"
#include "vectorClock.hpp"
#include "CXLSync.hpp"

extern thread_local ThreadOps *thread_ops;

// Should be power of two
constexpr uintptr_t CXL_NHC_RANGE = 1ull << 30;
constexpr uintptr_t CXL_HC_RANGE = 1ull << 20;
constexpr uintptr_t CXL_SYNC_RANGE = 1ull << 4;

// CXLPool should be in CXL-NHC memory
struct CXLPool {
    alignas(CACHE_LINE_SIZE) CXLMutex mutexes[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) CXLAtomic<char> atomic_data[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) char data[CXL_NHC_RANGE];

    CXLPool(): atomic_data{} {};
};

class User {
    // CXL mem shared data
    CXLPool &cxl_pool;

    unsigned node_id;
    // user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;
    unsigned blocked_count = 0;

public:
    User(CXLPool &pool, unsigned nid): cxl_pool(pool), node_id(nid){}

    inline void handle_store(char *addr, char val) {
        if (thread_ops->check_invalidate(addr)) {
#ifdef STATS
            invalidate_count++;
#endif
        }
        thread_ops->log_store(addr);
        *((volatile char *)addr) = val;
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
        //assume all atomic locations are cache coherent
        ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
        flush_fence();
    };

    inline void handle_store_raw(char *addr, char val) {
        do_invalidate(addr);
        invalidate_fence();
#ifdef STATS
        invalidate_count++;
#endif
        *((volatile char *)addr) = val;
        do_flush((char *)addr);
    }

    inline char handle_load_acquire_raw(char *addr) {
        //assume all atomic locations are cache coherent
        return ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
    };

    inline char handle_load_raw(char *addr) {
        do_invalidate(addr);
        invalidate_fence();
#ifdef STATS
        invalidate_count++;
#endif
        return *((volatile char *)addr);
    }

    void run();
};

#endif
