#ifndef _MEM_LAYOUT_H_
#define _MEM_LAYOUT_H_

#include <map>
#include <unordered_set>
#include <queue>

#include "cacheTracker.hpp"
#include "logBuffer.hpp"
#include "util.hpp"
    
static bool isAtomic(uintptr_t addr) {
    return addr < CXLMEM_ATOMIC_RANGE;
}

struct ALocMeta {
    Log* log;
    VectorClock clock;
};

// this emulates allocated atomic locations
using ALocMap = std::map<uintptr_t, Monitor<ALocMeta>>;

struct CXLMemMeta {
    PerNode<LogBuffer> buffers;
    //TODO: support dynamically allocated atomic locs
    ALocMap alocs;

    CXLMemMeta () {
        for (int i=0; i<CXLMEM_ATOMIC_RANGE; i++)
            alocs[i];
    }
};


struct CacheInfo {
    //TODO: fine-grained lock or atomics for each clock entry
    Monitor<VectorClock> clock;
    CacheLineTracker tracker;
    std::atomic<unsigned> consumed_count {0};

    void process_log(Log *log) {
        for (auto invalid_cl: *log) {
            tracker.mark_dirty(invalid_cl);
        }
    }

    VectorClock::clock_t update_clock(VectorClock::sized_t i) {
        return clock.mod([&](auto &self) {
            self.tick(i);
            return self[i];
        });
    }
};

struct NodeLocalMeta{
    CacheInfo cache_info;
    Monitor<VectorClock> user_clock;
};

#endif
