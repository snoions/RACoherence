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

constexpr uint64_t L1_BITS = 19;                      // [38:20]
constexpr uint64_t L2_BITS = 8;                       // [19:12]
constexpr uint64_t L1_ENTRIES = 1ull << L1_BITS;
constexpr uint64_t L2_ENTRIES = 1ull << L2_BITS;

class CacheLineTableLeaf {
public:
    // our model allows no concurrent access to same cache line
    // alternatively could use uint8_t dirty[CACHE_LINES_PER_PAGE]; to trade space for non-atomic access
    // the extra space could be used for other purposes
    std::atomic<uint64_t> dirty_mask{0};

    void mark_dirty(uint64_t line) {
        uint64_t mask = 1ull << line;
        dirty_mask.fetch_or(mask, std::memory_order_relaxed);
    }

    bool is_dirty(uint64_t line) const {
        uint64_t mask = 1ull << line;
        return dirty_mask.load(std::memory_order_relaxed) & mask;
    }

    void clear_dirty(uint64_t line) {
        uint64_t mask = ~(1ull << line);
        dirty_mask.fetch_and(mask, std::memory_order_relaxed);
    }
};

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

    void mark_dirty(virt_addr_t va) {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        get_or_create_leaf(l1_idx, l2_idx)->mark_dirty(line);
    }

    bool is_dirty(virt_addr_t va) const {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        auto* l2 = l1[l1_idx].load(std::memory_order_acquire);
        if (!l2) return false;

        auto* leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
        return leaf ? leaf->is_dirty(line) : false;
    }

    void clear_dirty(virt_addr_t va) {
        uint64_t l1_idx, l2_idx, line;
        split_va(va, l1_idx, l2_idx, line);
        auto* l2 = l1[l1_idx].load(std::memory_order_acquire);
        if (!l2) return;

        auto* leaf = l2->leaves[l2_idx].load(std::memory_order_acquire);
        if (leaf) leaf->clear_dirty(line);
    }

private:
    CacheLineTableLeaf* get_or_create_leaf(uint64_t l1_idx, uint64_t l2_idx) {
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

    static inline void split_va(virt_addr_t va, uint64_t &l1, uint64_t &l2, uint64_t &line) {
        line = (va >> 6) & (CACHE_LINES_PER_PAGE - 1);          // 6 bits
        l2   = (va >> 12) & (L2_ENTRIES - 1);                   // 8 bits
        l1   = (va >> 20) & (L1_ENTRIES - 1);                   // 19 bits
    }
};

#endif
