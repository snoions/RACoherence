#ifndef _CACHE_INFO_H_
#define _CACHE_INFO_H_

#include "clGroup.hpp"
#include "clTracker.hpp"
#include "flushUtils.hpp"
#include "logManager.hpp"
#include "utils.hpp"
#include "vectorClock.hpp"

namespace RACoherence {

using AtomicClock = std::atomic<VectorClock::clock_t>[NODE_COUNT];

struct CacheInfo {
    AtomicClock clock;
    // data-race on cach line tracker entries should be ruled out
    // by cache line race freedom.
    CacheLineTracker inv_cls;
    // per-node stats
    std::atomic<unsigned> consumed_count[NODE_COUNT];
    std::atomic<unsigned> produced_count;

    CacheInfo(): clock(), inv_cls(), consumed_count{}, produced_count{0} {};

    void process_log(Log &log) {
        struct InvalidateOp {
            CacheInfo &self;

            inline void operator() (uintptr_t ptr, uint64_t mask) {
#ifdef EAGER_INVALIDATE
                for (auto cl_addr: MaskCLRange(ptr, mask))
                    do_invalidate((char *)cl_addr);
#else
                self.inv_cls.mark_range_dirty(ptr, mask);
#endif
            }
        };

        for (auto invalid_cg: log) {
            process_cl_group(invalid_cg, InvalidateOp{*this});
        }
#ifdef EAGER_INVALIDATE
        invalidate_fence();
#endif
    }

    inline void update_clock(VectorClock::sized_t i, clock_t val) {
        clock[i].store(val, std::memory_order_relaxed);
    }

    inline VectorClock::clock_t get_clock(VectorClock::sized_t i) {
        return clock[i].load(std::memory_order_relaxed);
    }
};

} // RACoherence

#endif
