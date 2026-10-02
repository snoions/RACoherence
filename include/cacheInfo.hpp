#ifndef _CACHE_INFO_H_
#define _CACHE_INFO_H_

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <x86intrin.h>

#include "clGroup.hpp"
#include "clTracker.hpp"
#include "flushUtils.hpp"
#include "logManager.hpp"
#include "utils.hpp"
#include "vectorClock.hpp"

namespace RACoherence {

using AtomicClock = std::atomic<vc_clock_t>[NODE_COUNT];

// Most logs one consume_logs() call claims and processes from one source.
constexpr size_t LOG_MAX_BATCH = 100;

// ---------------------------------------------------------------------------------------
// Parallel consumption of one source's queue.
//
// Any number of threads of a node - cache agents and helping user threads - may consume the
// same source queue concurrently. Each claims a run of published logs, processes them
// (invalidates them eagerly, or records them in inv_cls), fences its own flushes, and marks
// them done. The committed prefix is then extended in order over consecutive done logs, and
// only then is the cache clock advanced (for release entries) and the node's head moved. So
// "clock >= k" still means every log up to k has been fully processed on this node, while
// the processing itself is parallel. No thread needs exclusive ownership of a queue, which
// is what the per-source head mutex used to provide.
// ---------------------------------------------------------------------------------------
static_assert((LOG_COUNT & (LOG_COUNT - 1)) == 0, "LOG_COUNT must be a power of two");

struct SourceConsumeState {
    using idx_t = LogManager::idx_t;
    // Next log index to claim.
    alignas(CACHE_LINE_SIZE) std::atomic<idx_t> claim{0};
    // Node-local copy of this node's head in the source's LogManager: every log with index
    // < local_head is done and its effects are published. Committing threads compete on this
    // copy (a cheap local CAS), then advance the shared head to match (advance_head).
    alignas(CACHE_LINE_SIZE) std::atomic<idx_t> local_head{0};
    // Slot j % LOG_COUNT holds ((j + 1) << 1) | is_release once log j is done. Storing j + 1
    // rather than a flag means a slot from an earlier trip round the ring can never be
    // mistaken for log j.
    alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> done[LOG_COUNT];
};

struct CacheInfo {
    using idx_t = LogManager::idx_t;
    static constexpr idx_t NO_LIMIT = std::numeric_limits<idx_t>::max();

    AtomicClock clock;
    SourceConsumeState consume[NODE_COUNT];
    CacheLineTracker inv_cls;

    // per-node stats
    std::atomic<unsigned> consumed_count[NODE_COUNT];
    std::atomic<unsigned> produced_count;

    CacheInfo(): clock(), consume(), inv_cls(), consumed_count{}, produced_count{0} {};

    // Claim up to `max` (capped at LOG_MAX_BATCH) published, unclaimed logs of source `src`
    // with index below `upto`, process them, mark them done and extend the committed prefix.
    // Returns how many logs this call processed; 0 if none were available to claim.
    // `log_mgrs` is the array of all nodes' LogManagers and `self` this node's id.
    // Safe to call from any thread of this node, concurrently with any other caller.
    size_t consume_logs(LogManager *log_mgrs, unsigned self, unsigned src, size_t max, idx_t upto = NO_LIMIT) {
        SourceConsumeState &st = consume[src];
        LogManager &lm = log_mgrs[src];
        max = std::min(max, LOG_MAX_BATCH);

        // 1. Claim a run of consecutive published, unclaimed logs with one CAS. Claiming only
        //    what is already published means no thread holds an index it has to wait on.
        idx_t first = st.claim.load(std::memory_order_acquire);
        size_t n;
        for (;;) {
            n = 0;
            while (n < max && first + n < upto && lm.published_entry(first + n)) n++;
            if (n == 0) return 0;
            if (st.claim.compare_exchange_weak(first, first + n, std::memory_order_acq_rel,
                                               std::memory_order_acquire))
                break;
            // Lost the race: `first` now holds the current claim index; rescan from there.
        }

        // 2. Process the claimed logs. Their entries cannot be reused under us: the producer
        //    only recycles a log once every subscriber's head has passed it, and this node's
        //    head only moves past a log once it is committed. inv_cls supports concurrent
        //    inserts, so lazy mode needs no lock either.
        bool is_rel[LOG_MAX_BATCH];
        for (size_t k = 0; k < n; k++) {
            const LogManager::PubEntry *entry = lm.published_entry(first + k);
            assert(entry && "claimed log is no longer published");
            is_rel[k] = entry->is_rel;
            process_log(*entry->log.load(std::memory_order_relaxed));
            STATS(consumed_count[src]++;)
        }

#if EAGER_INVALIDATE
        // This thread's own flushes must complete before the logs are marked done. An x86
        // fence only waits for flushes issued by the same core, so no other thread can do
        // this for us.
        invalidate_fence();
#endif

        // 3. Mark done, then extend the committed prefix as far as consecutive done logs allow.
        //    Release stores plus one seq_cst fence per batch, rather than a seq_cst store
        //    (an xchg on x86) per log; see commit_logs() for why the fence is needed.
        for (size_t k = 0; k < n; k++) {
            const idx_t j = first + k;
            st.done[slot(j)].store(done_word(j, is_rel[k]), std::memory_order_release);
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
        commit_logs(log_mgrs, self, src);
        return n;
    }

    // Extend the committed prefix of `src` over consecutive done logs, advancing the clock
    // for release entries and this node's head. consume_logs() calls this itself.
    //
    // A whole run of consecutive done logs is committed with one CAS on `local_head`, and the
    // clock and head are each updated once per run, to its last release and its end. Only
    // the thread whose CAS succeeds commits a given run, so each log is still committed
    // exactly once. Committers of consecutive runs may publish out of order, so both updates
    // are monotonic: neither the clock nor the head may move backwards.
    //
    // The done/local_head handshake is a store-buffering pattern: a finisher stores done[j]
    // and then reads `local_head`, while a committer advances `local_head` and then reads
    // done[j]. Without ordering each could miss the other's write and leave log j
    // uncommitted until some other thread came along. The finisher's seq_cst fence (after its
    // done stores, in consume_logs) together with the seq_cst CAS and loads here rules that
    // out: whichever comes first in the seq_cst order, the other side observes it.
    void commit_logs(LogManager *log_mgrs, unsigned self, unsigned src) {
        SourceConsumeState &st = consume[src];
        idx_t c = st.local_head.load(std::memory_order_seq_cst);
        for (;;) {
            // Find the run of consecutive done logs starting at c, and its last release.
            idx_t k = 0;
            idx_t last_rel = 0;  // clock value for the run's last release; 0 if none
            while (k < LOG_COUNT) {
                const uint64_t v = st.done[slot(c + k)].load(std::memory_order_seq_cst);
                if ((v >> 1) != static_cast<uint64_t>(c + k) + 1) break;  // log c + k not done
                if (v & 1) last_rel = c + k + 1;
                k++;
            }
            if (k == 0) return;
            // On failure `c` holds the current value (another thread committed past it) and
            // the scan restarts from there.
            if (!st.local_head.compare_exchange_weak(c, c + k, std::memory_order_seq_cst))
                continue;
            if (last_rel) update_clock_monotonic(src, last_rel);
            log_mgrs[src].advance_head(self, c + k);
            LOG_DEBUG("node " << self << " committed logs " << c << ".." << c + k - 1 << " from " << src
                              << " clock=" << get_clock(src))
            c += k;
        }
    }

    // Restart `src`'s consumption at index `start`, e.g. after (re)subscribing to it. Must
    // not run concurrently with consumers of `src`.
    void reset_consume(unsigned src, idx_t start) {
        SourceConsumeState &st = consume[src];
        st.claim.store(start, std::memory_order_relaxed);
        st.local_head.store(start, std::memory_order_relaxed);
        for (auto &d : st.done) d.store(0, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void process_log(Log &log) {
        for (const auto &entry: log)
            process_log_entry(entry);
    }

    inline void update_clock(VectorClock::sized_t i, vc_clock_t val) {
        clock[i].store(val, std::memory_order_relaxed);
    }

    inline void update_clock_monotonic(VectorClock::sized_t i, vc_clock_t val)
{       auto old = clock[i].load(std::memory_order_relaxed);
        if (val <= old)
            return;
        while(!clock[i].compare_exchange_weak(old, val))
        {
            if (val <= old)
                return;
        }
    }

    inline vc_clock_t get_clock(VectorClock::sized_t i) {
        return clock[i].load(std::memory_order_relaxed);
    }

    void dump_stats() {
        for (int i = 0; i < NODE_COUNT; i++)
	        LOG_STATS("consumed count from node " << i << ": " << consumed_count[i].load());
    }

private:

    static size_t slot(idx_t j) { return static_cast<size_t>(j) & (LOG_COUNT - 1); }

    static uint64_t done_word(idx_t j, bool is_rel) {
        return (static_cast<uint64_t>(j + 1) << 1) | (is_rel ? 1u : 0u);
    }

    void process_log_entry(const cl_group_t entry) {
        using namespace cl_group;
#if !LOCAL_CL_TABLE
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
};

} // RACoherence

#endif
