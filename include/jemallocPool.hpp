#ifndef _JEMALLOC_POOL_H_
#define _JEMALLOC_POOL_H_

#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "logger.hpp"
#include "jemalloc/jemalloc.h"
#include "clh_mutex.hpp"
#include "cxlMalloc.hpp"

template<typename K, typename T>
using cxlhc_map = std::map<K, T, std::less<K>,  cxlhc_allocator<std::pair<const K, T>>> ;
template<typename T>
using cxlhc_set = std::set<T, std::less<T>, cxlhc_allocator<T>> ;
template<typename K, typename T>
using cxlhc_unordered_map = std::unordered_map<K, T, std::hash<K>,  std::equal_to<K>, cxlhc_allocator<std::pair<const K, T>>> ;
template<typename T>
using cxlhc_vector = std::vector<T, cxlhc_allocator<T>>;

/* ExtentPool + jemalloc extent hooks can be used to allocate from a custom pool. 
 * Though has to make sure jemalloc is compiled with --without-export to not override
 * the default allocator, mainly because tcache can only come from one arena, causing pool memory to be used for normal allocation as well. 
 */
class ExtentPool {
public:
    ExtentPool(void* buffer, size_t buffer_size)
        : base_(reinterpret_cast<uint8_t*>(buffer)), size_(buffer_size), offset_(0)
    {
        page_size_ = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        if (page_size_ == 0) page_size_ = 4096;
    }

    // Try to allocate exact (size, alignment). Must return either exact requested size/aligned ptr or nullptr.
    void* alloc_extent(size_t size, size_t alignment) {
        // 1) Fast exact-size bucket reuse
        {
            std::lock_guard lg(meta_mtx_);
            auto it = per_size_buckets_.find(size);
            if (it != per_size_buckets_.end() && !it->second.empty()) {
                void* p = it->second.back();
                it->second.pop_back();
                return p;
            }
        }

        // 2) Try to find a free region with sufficient size (best-fit style via free_by_size)
        {
            std::lock_guard lg(meta_mtx_);
            auto fit = free_by_size_.lower_bound(size);
            while (fit != free_by_size_.end()) {
                size_t region_size = fit->first;
                // find a start address in the set
                auto &addrs = fit->second;
                if (addrs.empty()) {
                    // shouldn't happen, but just in case erase and continue
                    fit = free_by_size_.erase(fit);
                    continue;
                }
                uintptr_t start = *addrs.begin();
                // compute alignment adjustment
                uintptr_t base_addr = reinterpret_cast<uintptr_t>(base_);
                uintptr_t candidate = start;
                uintptr_t aligned = ( (candidate + (alignment - 1)) & ~(alignment - 1) );
                size_t adjust = static_cast<size_t>(aligned - candidate);
                if (adjust + size <= region_size) {
                    // accept this region; remove from maps and possibly split
                    // remove addr from free_by_size set
                    addrs.erase(addrs.begin());
                    if (addrs.empty()) free_by_size_.erase(fit);
                    // remove region from free_by_addr
                    auto addr_it = free_by_addr_.find(start);
                    assert(addr_it != free_by_addr_.end());
                    size_t orig_size = addr_it->second;
                    free_by_addr_.erase(addr_it);

                    // if there's prefix before aligned, reinsert it
                    if (adjust > 0) {
                        uintptr_t prefix_start = start;
                        size_t prefix_size = adjust;
                        insert_free_region_locked(prefix_start, prefix_size);
                    }
                    // computed allocated start = aligned
                    uintptr_t alloc_start = aligned;
                    // if leftover suffix after allocation, insert remainder
                    size_t used = adjust + size;
                    if (orig_size > used) {
                        uintptr_t suffix_start = start + used;
                        size_t suffix_size = orig_size - used;
                        insert_free_region_locked(suffix_start, suffix_size);
                    }
                    void* p = reinterpret_cast<void*>(alloc_start);
                    return p;
                } else {
                    // this region cannot satisfy alignment-adjusted request; try next candidate in same size set,
                    // but if none fit in this size, try next larger size
                    ++fit;
                }
            }
        }

        // 3) Bump allocate (lock-free CAS on offset_)
        size_t cur = offset_.load(std::memory_order_relaxed);
        uintptr_t base_addr = reinterpret_cast<uintptr_t>(base_);
        while (true) {
            uintptr_t start_addr = base_addr + cur;
            uintptr_t aligned = ( (start_addr + (alignment - 1)) & ~(alignment - 1) );
            size_t aligned_offset = static_cast<size_t>(aligned - base_addr);
            if (aligned_offset + size > size_) {
                // out of pool
                return nullptr;
            }
            size_t next = aligned_offset + size;
            if (offset_.compare_exchange_weak(cur, next,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
                void* p = reinterpret_cast<void*>(aligned);
                return p;
            }
            // CAS failed, cur updated -> retry
        }
    }

    // Deallocate an extent previously allocated (exact ptr and size)
    void dealloc_extent(void* ptr, size_t size) {
        if (!ptr || size == 0) return;
        uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
        std::lock_guard lg(meta_mtx_);

        // Coalesce with neighbors if present
        uintptr_t region_start = start;
        size_t region_size = size;

        // find the first region whose start > start (upper_bound), then check left neighbor
        auto hi = free_by_addr_.lower_bound(start + 1); // first start > start
        // check left neighbor (previous entry)
        if (hi != free_by_addr_.begin()) {
            auto lo = std::prev(hi);
            uintptr_t lo_start = lo->first;
            size_t lo_size = lo->second;
            if (lo_start + lo_size == start) {
                // merge left neighbor
                region_start = lo_start;
                region_size += lo_size;
                // remove lo from maps
                remove_free_region_locked(lo_start, lo_size);
            }
        }

        // check right neighbor (hi might be the right neighbor)
        if (hi != free_by_addr_.end()) {
            uintptr_t hi_start = hi->first;
            size_t hi_size = hi->second;
            if (start + size == hi_start) {
                // merge right neighbor
                region_size += hi_size;
                remove_free_region_locked(hi_start, hi_size);
            }
        }

        // insert the coalesced region
        insert_free_region_locked(region_start, region_size);

        // Additionally, if this region exactly matches a "common" size, push it to per_size_buckets_
        auto it_bucket = per_size_buckets_.find(region_size);
        if (it_bucket != per_size_buckets_.end()) {
            // pull region out of free_by_addr/free_by_size and put into bucket
            remove_free_region_locked(region_start, region_size);
            it_bucket->second.push_back(reinterpret_cast<void*>(region_start));
        }
    }

    // Register a per-size bucket (call before using pool if you expect many repeats of that size)
    void ensure_bucket_for_size(size_t size) {
        std::lock_guard lg(meta_mtx_);
        per_size_buckets_.try_emplace(size);
    }

    size_t page_size() const { return page_size_; }
    size_t total_size() const { return size_; }

private:
    uint8_t* base_;
    size_t size_;
    std::atomic<size_t> offset_;
    size_t page_size_;

    // metadata protected by meta_mtx_
    CLHMutex meta_mtx_;
    //std::mutex meta_mtx_;

    // map from start_address -> size (free regions), ordered by start address for coalescing
    cxlhc_map<uintptr_t, size_t> free_by_addr_;

    // map from size -> set of start addresses; ordered by size, so lower_bound finds smallest suitable
    cxlhc_map<size_t, cxlhc_set<uintptr_t>> free_by_size_;
    // per-size exact buckets for very fast reuse
    cxlhc_unordered_map<size_t, cxlhc_vector<void*>> per_size_buckets_;

    // helper: insert free region (locked)
    void insert_free_region_locked(uintptr_t start, size_t sz) {
        if (sz == 0) return;
        free_by_addr_.emplace(start, sz);
        free_by_size_[sz].insert(start);
    }

    // helper: remove free region from both maps (locked)
    void remove_free_region_locked(uintptr_t start, size_t sz) {
        auto it = free_by_addr_.find(start);
        if (it != free_by_addr_.end()) free_by_addr_.erase(it);
        auto it2 = free_by_size_.find(sz);
        if (it2 != free_by_size_.end()) {
            it2->second.erase(start);
            if (it2->second.empty()) free_by_size_.erase(it2);
        }
    }
};

#endif
