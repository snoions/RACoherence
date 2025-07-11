#ifndef _MEM_LAYOUT_H_
#define _MEM_LAYOUT_H_

#include "CLTracker.hpp"
#include "logBuffer.hpp"
#include "util.hpp"
#include "vectorClock.hpp"

// Should be power of two
constexpr uintptr_t CXLMEM_RANGE = 1ull << 20; // 1KB, small size to simulate locality
constexpr uintptr_t CXLMEM_ATOMIC_RANGE = 1ull << 4;

struct AtomicMeta {
    VectorClock clock;
};

// this emulates allocated atomic locations
using AtomicMap = std::array<Monitor<AtomicMeta>, CXLMEM_ATOMIC_RANGE>;

struct CXLMemMeta {
    PerNode<LogBuffer> bufs = {};
    //TODO: support dynamically allocated atomic locs
    AtomicMap atmap = {};
};

constexpr size_t CXL_DATA_PADDING = CACHE_LINE_SIZE - (sizeof(CXLMemMeta) % CACHE_LINE_SIZE);

struct CXLPool {
    CXLMemMeta meta;
    char padding[CXL_DATA_PADDING];
    char data[CXLMEM_RANGE];
};

using AtomicClock = std::array<std::atomic<VectorClock::clock_t>, NODE_COUNT>;

struct CacheInfo {
    AtomicClock clock{};
    // data-race on cach line tracker entries should be ruled out
    // by cache line race freedom.
    CacheLineTracker inv_cls;
    // per-node stats
    std::atomic<unsigned> consumed_count {0};
    std::atomic<unsigned> produced_count {0};

    void process_log(Log &log) {
        for (auto invalid_cl: log) {
            inv_cls.mark_range_dirty(get_ptr(invalid_cl), get_mask64(invalid_cl));
        }
    }

    bool is_dirty(char *addr) {
        return inv_cls.is_dirty((uintptr_t)addr);
    }

    VectorClock::clock_t update_clock(VectorClock::sized_t i) {
        return ++clock[i];
    }
    
    VectorClock::clock_t get_clock(VectorClock::sized_t i) {
        return clock[i].load();
    }
};

struct NodeLocalMeta{
    CacheInfo cache_info;
    // user_clock currently shared by all user threads on the same node, could also have smaller groups of user threads share clocks
    Monitor<VectorClock> user_clock;
};

#endif
