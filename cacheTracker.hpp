#ifndef _CACHE_TRACKER_H_
#define _CACHE_TRACKER_H_

#include <iostream>
#include <bitset>
#include <memory>
#include <mutex>
#include <tuple>
#include <cstdint>

#include "config.hpp"

constexpr uint64_t L1_BITS = 19;                      // [38:20]
constexpr uint64_t L2_BITS = 8;                       // [19:12]
constexpr uint64_t L1_ENTRIES = 1ull << L1_BITS;
constexpr uint64_t L2_ENTRIES = 1ull << L2_BITS;

// Leaf node: stores dirty cache line bitmap
class CacheLineTableLeaf {
public:
    std::bitset<CACHE_LINES_PER_PAGE> dirty;
    mutable std::mutex mtx;

    void mark_dirty(uint64_t line) {
        std::lock_guard<std::mutex> lock(mtx);
        dirty.set(line);
    }

    bool is_dirty(uint64_t line) const {
        std::lock_guard<std::mutex> lock(mtx);
        return dirty.test(line);
    }

    void clear_dirty(uint64_t line) {
        std::lock_guard<std::mutex> lock(mtx);
        dirty.reset(line);
    }
};

// Level-2 node (per 4KB page)
class CacheLineTableL2 {
public:
    std::unique_ptr<CacheLineTableLeaf> leaves[L2_ENTRIES]{};
};

// Top-level tracker
class CacheLineTracker {
    std::unique_ptr<CacheLineTableL2> l1[L1_ENTRIES]{};

public:
    void mark_dirty(virt_addr_t va) {
        auto [l1_idx, l2_idx, line] = split_va(va);
        ensure_leaf(l1_idx, l2_idx)->mark_dirty(line);
    }

    bool is_dirty(virt_addr_t va) const {
        auto [l1_idx, l2_idx, line] = split_va(va);
        const auto* leaf = get_leaf(l1_idx, l2_idx);
        return leaf ? leaf->is_dirty(line) : false;
    }

    void clear_dirty(virt_addr_t va) {
        auto [l1_idx, l2_idx, line] = split_va(va);
        if (auto* leaf = get_leaf(l1_idx, l2_idx)) {
            leaf->clear_dirty(line);
        }
    }

private:
    CacheLineTableLeaf* ensure_leaf(uint64_t l1_idx, uint64_t l2_idx) {
        if (!l1[l1_idx]) {
            l1[l1_idx] = std::make_unique<CacheLineTableL2>();
        }
        auto& l2 = l1[l1_idx];
        if (!l2->leaves[l2_idx]) {
            l2->leaves[l2_idx] = std::make_unique<CacheLineTableLeaf>();
        }
        return l2->leaves[l2_idx].get();
    }

    CacheLineTableLeaf* get_leaf(uint64_t l1_idx, uint64_t l2_idx) const {
        auto* l2 = l1[l1_idx].get();
        if (!l2) return nullptr;
        return l2->leaves[l2_idx].get();
    }

    static std::tuple<uint64_t, uint64_t, uint64_t> split_va(virt_addr_t va) {
        uint64_t line = (va >> 6) & (CACHE_LINES_PER_PAGE - 1);          // 6 bits
        uint64_t l2   = (va >> 12) & (L2_ENTRIES - 1);                   // 8 bits
        uint64_t l1   = (va >> 20) & (L1_ENTRIES - 1);                   // 19 bits
        return {l1, l2, line};
    }
};
#endif
