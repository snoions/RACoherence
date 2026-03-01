#ifndef _CACHE_INFO_H_
#define _CACHE_INFO_H_

#include <x86intrin.h>

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
    //std::chrono::duration<double> process_log_duration{0};
    //unsigned process_log_count = 0;

    CacheInfo(): clock(), inv_cls(), consumed_count{}, produced_count{0} {};
    //void simulate_process_log(Log &log) {
    //    using namespace cl_group;
    //    STATS(auto start = std::chrono::steady_clock::now();)
    //    for (auto entry: log) {
    //        if (is_length_based(entry)) {
    //            unsigned length = get_length(entry);
    //            uintptr_t cl_addr = get_ptr(entry);
    //            for (unsigned i = 0; i < length * GROUP_SIZE * CL_EXPAND_FACTOR; i++) {
    //                __rdtsc();
    //                __rdtsc();
    //            }
    //        } else {
    //            for (auto cl_addr: MaskCLRange(get_ptr(entry), get_mask16(entry)))
    //                // should be unrolled, manually unroll if not
    //                for (unsigned i = 0; i < CL_EXPAND_FACTOR; i++) {
    //                   __rdtsc();
    //                   __rdtsc();
    //                }
    //        }
    //    }
    //    STATS(
    //        auto end = std::chrono::steady_clock::now();
    //        process_log_duration += end - start;
    //        process_log_count++;
    //    )
    //}

    void process_log(Log &log) {
        using namespace cl_group;
        for (auto entry: log) {
#if IMMEDIATE_PUBLISH
            do_invalidate((char *)(entry << VIRTUAL_CL_SHIFT));
#else
            if (is_length_based(entry)) {
                unsigned length = get_length(entry);
#ifdef WBINVD_PATH
                if (length >= WBINVD_THRESHOLD) {
                    wbinvd();
                    return;
                }
#endif
                uintptr_t cl_addr = get_ptr(entry);
#if EAGER_INVALIDATE
                for (unsigned i = 0; i < length * GROUP_SIZE * CL_EXPAND_FACTOR; i++)
                    do_invalidate((char *)cl_addr + i * CACHE_LINE_SIZE);
#else
                for (unsigned i = 0; i < length; i++)
                    inv_cls.mark_dirty(cl_addr + i, FULL_MASK << get_mask16_to_64_shift(cl_addr));
#endif
            } else {
#if EAGER_INVALIDATE
                for (auto cl_addr: MaskCLRange(get_ptr(entry), get_mask16(entry)))
                    // should be unrolled, manually unroll if not
                    for (unsigned i = 0; i < CL_EXPAND_FACTOR; i++)
                        do_invalidate((char *)cl_addr + i * CACHE_LINE_SIZE);
#else
                inv_cls.mark_dirty(get_ptr(entry),  get_mask16(entry) << get_mask16_to_64_shift(entry));
#endif
            }
#endif
        }
    }

    inline void update_clock(VectorClock::sized_t i, clock_t val) {
        clock[i].store(val, std::memory_order_relaxed);
    }

    inline VectorClock::clock_t get_clock(VectorClock::sized_t i) {
        return clock[i].load(std::memory_order_relaxed);
    }

    void dump_stats() {
        for (int i = 0; i < NODE_COUNT; i++)
	        std::cout << "consumed count from node " << i << ": " << consumed_count[i].load() << std::endl;
    }
};

} // RACoherence

#endif
