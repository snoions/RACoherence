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
        using namespace cl_group;
        for (auto cg: log) {
            if (is_length_based(cg)) {
                for (auto cl_addr: LengthCLRange(cg)) {
#ifdef EAGER_INVALIDATE
                    // should be unrolled, manually unroll if not
                    for (unsigned i = 0; i < GROUP_SIZE * CL_UNIT_GRANULARITY; i++)
                        do_invalidate((char *)cl_addr + (i * CACHE_LINE_SIZE));
#else
                    self.inv_cls.mark_range_dirty(cl_addr, FULL_MASK << get_mask16_to_64_shift(cl_addr));
#endif
                }
            } else {
#ifdef EAGER_INVALIDATE
                for (auto cl_addr: MaskCLRange(get_ptr(cg), get_mask16(cg)))
                    // should be unrolled, manually unroll if not
                    for (unsigned i = 0; i < CL_UNIT_GRANULARITY; i++)
                        do_invalidate((char *)cl_addr + i * CACHE_LINE_SIZE);
#else
                self.inv_cls.mark_range_dirty(get_ptr(cg),  get_mask16(cg) << get_mask16_to_64_shift(cg));
#endif
            }
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
