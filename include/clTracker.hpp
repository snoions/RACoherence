#ifndef _CACHE_TRACKER_H_
#define _CACHE_TRACKER_H_

#include <atomic>
#include <bitset>
#include <memory>
#include <shared_mutex>
#include <tuple>
#include <cstdint>
#include <array>

#include "config.hpp"
#include "flushUtils.hpp"

//TODO: track cl unit instead of cache line
namespace RACoherence {

constexpr uint64_t L1_BITS = 19;                      // [38:20]
constexpr uint64_t L2_BITS = 8;                       // [19:12]
constexpr uint64_t L1_ENTRIES = 1ull << L1_BITS;
constexpr uint64_t L2_ENTRIES = 1ull << L2_BITS;

//class CacheLineTableLeaf {
//public:
//    uint8_t dirty[CACHE_LINES_PER_PAGE];
//
//    void mark_range_dirty(uint64_t mask) {
//        while(mask) {
//            unsigned p = __builtin_ctzl(mask);
//            dirty[p] = 1;
//            mask ^= mask &-mask;
//        }
//    }
//
//    void mark_dirty(uint64_t line) {
//        dirty[line] = 1;
//    }
//
//    bool is_dirty(uint64_t line) const {
//        return dirty[line] == 0;
//    }
//
//    void clear_dirty(uint64_t line) {
//        dirty[line] = 0;
//    }
//};

class CacheLineTableLeaf {
public:
    //non-atomic is safe assuming no races on cache line, so uint8_t dirty[CACHE_LINES_PER_PAGE] is another option
    std::atomic<uint64_t> dirty_mask{0};

    inline uint64_t mark_dirty_with_mask(uint64_t mask) {
        return dirty_mask.fetch_or(mask, std::memory_order_relaxed);
    }

    inline void mark_dirty(uint64_t line) {
        uint64_t mask = 1ull << line;
        dirty_mask.fetch_or(mask, std::memory_order_relaxed);
    }

    inline bool is_dirty(uint64_t line) const {
        uint64_t mask = 1ull << line;
        return dirty_mask.load(std::memory_order_relaxed) & mask;
    }

    inline uint64_t clear_dirty_with_mask(uint64_t mask) {
        return dirty_mask.fetch_and(~mask, std::memory_order_relaxed);
    }

    inline void clear_dirty(uint64_t line) {
        uint64_t mask = ~(1ull << line);
        dirty_mask.fetch_and(mask, std::memory_order_relaxed);
    }
};

//TODO: try pre-initialize all tables
class CacheLineTableL2 {
public:
    std::atomic<CacheLineTableLeaf*> leaves[L2_ENTRIES]{};
};

class CacheLineTracker {
    std::atomic<CacheLineTableL2*> l1[L1_ENTRIES]{};

public:
    ~CacheLineTracker() {
        for (uint64_t i = 0; i < L1_ENTRIES; ++i) {
            auto* l2 = l1[i].load(std::memory_order_relaxed);
            if (!l2) continue;

            for (uint64_t j = 0; j < L2_ENTRIES; ++j) {
                auto* leaf = l2->leaves[j].load(std::memory_order_relaxed);
                delete leaf;  // OK if nullptr
            }
            delete l2;
        }
    }

    void mark_dirty(uintptr_t va) {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        get_or_create_leaf(l1_idx, l2_idx)->mark_dirty(line);
    }

    void mark_range_dirty(uintptr_t va, uint64_t mask) {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        get_or_create_leaf(l1_idx, l2_idx)->mark_dirty_with_mask(mask);
    }

    bool is_dirty(uintptr_t va) const {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        auto* l2 = l1[l1_idx].load(std::memory_order_acquire);
        if (!l2) return false;

        auto* leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
        return leaf ? leaf->is_dirty(line) : false;
    }

    bool invalidate_if_dirty(uintptr_t va) {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        auto* l2 = l1[l1_idx].load(std::memory_order_acquire);
        if (!l2) return false;

        auto* leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
        if (leaf && leaf->is_dirty(line)) {
            do_invalidate((char *)va);
            invalidate_fence();
            leaf->clear_dirty(line);
            return true;
        }
        return false;
    }


    bool invalidate_range_if_dirty(uintptr_t begin, uintptr_t end) {
        bool any_dirty = false;

        // Align begin to cache line boundary
        uintptr_t addr = begin & ~(CACHE_LINE_SIZE - 1);

        while (addr < end) {
            uint64_t l1_idx, l2_idx, line_idx;
            split_va(addr, l1_idx, l2_idx, line_idx);

            auto* l2 = l1[l1_idx].load(std::memory_order_acquire);
            if (!l2) {
                // Jump to next leaf
                addr = ((addr >> 12) + 1) << 12;
                continue;
            }

            auto* leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
            if (!leaf) {
                // Jump to next leaf
                addr = ((addr >> 12) + 1) << 12;
                continue;
            }

            // Calculate how many cache lines in this page we need to process
            uint64_t start_line = line_idx;
            uint64_t end_line = CACHE_LINES_PER_PAGE;

            uintptr_t page_end_va = ((addr >> 12) + 1) << 12; // next page boundary
            if (page_end_va > end) {
                // Clamp to range end
                end_line = (end >> 6) & (CACHE_LINES_PER_PAGE - 1);
            }

            // Construct a mask of all lines in [start_line, end_line)
            uint64_t mask;
            if (end_line == 64) {
                mask = ~0ull << start_line;   // full tail mask
            } else {
                mask = ((1ull << end_line) - 1) & ~((1ull << start_line) - 1);
            }

            // Atomically clear all those bits
            // might allow more parallelism for flushes if all fetch_and are moved to end of function
            uint64_t prev = leaf->clear_dirty_with_mask(mask);
            uint64_t was_dirty = prev & mask;

            if (was_dirty) {
                any_dirty = true;

                // For each bit set in was_dirty, invalidate that line
                uint64_t bits = was_dirty;
                while (bits) {
                    unsigned bit = __builtin_ctzll(bits);  // index of lowest set bit
                    uintptr_t line_va = (addr & ~((1ull << 12) - 1)) + (bit * CACHE_LINE_SIZE);
                    do_invalidate((char*)line_va);
                    bits &= bits - 1; // clear lowest set bit
                }
            }

            // Advance to next page (next leaf in L2)
            addr = page_end_va;
        }

        return any_dirty;
    }

    void clear_dirty(uintptr_t va) {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        auto* l2 = l1[l1_idx].load(std::memory_order_acquire);
        if (!l2) return;

        auto* leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
        if (leaf) leaf->clear_dirty(line);
    }

private:
    inline CacheLineTableLeaf* get_or_create_leaf(uint64_t l1_idx, uint64_t l2_idx) {
        auto* l2 = l1[l1_idx].load(std::memory_order_acquire);
        if (!l2) {
            auto* new_l2 = new CacheLineTableL2();
            if (!l1[l1_idx].compare_exchange_strong(l2, new_l2, std::memory_order_acq_rel)) {
                delete new_l2;
                l2 = l1[l1_idx].load(std::memory_order_acquire);
            } else {
                l2 = new_l2;
            }
        }

        auto* leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
        if (!leaf) {
            auto* new_leaf = new CacheLineTableLeaf();
            if (!l2->leaves[l2_idx].compare_exchange_strong(leaf, new_leaf, std::memory_order_acq_rel)) {
                delete new_leaf;
                leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
            } else {
                leaf = new_leaf;
            }
        }
        return leaf;
    }

    static inline void split_va(uintptr_t va, uint64_t &l1, uint64_t &l2, uint64_t &line) {
        line = (va >> 6) & (CACHE_LINES_PER_PAGE - 1);          // 6 bits
        l2   = (va >> 12) & (L2_ENTRIES - 1);                   // 8 bits
        l1   = (va >> 20) & (L1_ENTRIES - 1);                   // 19 bits
    }
};

} // RACoherence

#endif
