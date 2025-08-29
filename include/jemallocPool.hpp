#ifndef _JEMALLOC_POOL_H_
#define _JEMALLOC_POOL_H_

#include <map>
#include <set>
#include <unordered_map>
#include <unistd.h>
#include <vector>

#include "jemalloc/jemalloc.h"

/* ExtentPool + jemalloc extent hooks can be used to allocate from a custom pool. 
 * Though has to make sure jemalloc is compiled with --without-export to not override
 * the default allocator, mainly because tcache can only come from one arena, causing pool memory to be used for normal allocation as well. 
 */
//TODO: replace alloactor for std data structures
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
            std::lock_guard<std::mutex> lg(meta_mtx_);
            auto it = per_size_buckets_.find(size);
            if (it != per_size_buckets_.end() && !it->second.empty()) {
                void* p = it->second.back();
                it->second.pop_back();
                std::cout << "[ExtentPool2] reuse exact bucket ptr=" << p << " size=" << size << "\n";
                return p;
            }
        }

        // 2) Try to find a free region with sufficient size (best-fit style via free_by_size)
        {
            std::lock_guard<std::mutex> lg(meta_mtx_);
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
                    std::cout << "[ExtentPool2] reused region ptr=" << p << " size=" << size
                              << " (orig_region=" << start << "/" << orig_size << ") align=" << alignment << "\n";
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
                std::cout << "[ExtentPool2] bump alloc ptr=" << p << " size=" << size
                          << " alignment=" << alignment << "\n";
                return p;
            }
            // CAS failed, cur updated -> retry
        }
    }

    // Deallocate an extent previously allocated (exact ptr and size)
    void dealloc_extent(void* ptr, size_t size) {
        if (!ptr || size == 0) return;
        uintptr_t start = reinterpret_cast<uintptr_t>(ptr);
        std::lock_guard<std::mutex> lg(meta_mtx_);

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
            std::cout << "[ExtentPool2] dealloc placed into exact bucket ptr=" << reinterpret_cast<void*>(region_start)
                      << " size=" << region_size << "\n";
        } else {
            std::cout << "[ExtentPool2] dealloc inserted region ptr=" << reinterpret_cast<void*>(region_start)
                      << " size=" << region_size << "\n";
        }
    }

    // Register a per-size bucket (call before using pool if you expect many repeats of that size)
    void ensure_bucket_for_size(size_t size) {
        std::lock_guard<std::mutex> lg(meta_mtx_);
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
    std::mutex meta_mtx_;

    // map from start_address -> size (free regions), ordered by start address for coalescing
    std::map<uintptr_t, size_t> free_by_addr_;

    // map from size -> set of start addresses; ordered by size, so lower_bound finds smallest suitable
    std::map<size_t, std::set<uintptr_t>> free_by_size_;

    // per-size exact buckets for very fast reuse
    std::unordered_map<size_t, std::vector<void*>> per_size_buckets_;

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

ExtentPool* g_extent_pool;
unsigned my_arena_index;

static void* my_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    std::cout << "[hook] extent_alloc requested size=" << size << " align=" << alignment << "\n";
    if (!g_extent_pool) return nullptr;

    // jemalloc expects exact size and alignment. Our pool will satisfy it or return nullptr.
    void* p = g_extent_pool->alloc_extent(size, alignment);
    if (!p) {
        std::cout << "[hook] alloc_extent failed (out of pool)\n";
        return nullptr;
    }

    if (zero && *zero) {
        // jemalloc says it wants zeroed memory. Zero it for safety.
        std::memset(p, 0, size);
        *zero = false; // we satisfied zero
    }
    if (commit) *commit = true;

    return p;
}

static bool my_extent_dalloc(extent_hooks_t* /*hooks*/,
                             void* addr, size_t size, bool /*committed*/, unsigned /*arena_ind*/) {
    std::cout << "[hook] extent_dalloc addr=" << addr << " size=" << size << "\n";
    if (!g_extent_pool) return true; // claim we handled it
    g_extent_pool->dealloc_extent(addr, size);
    // Return true means "jemalloc should not try to free it again" — semantics vary;
    // jemalloc expects 'true' when the hook freed it, but many examples return false.
    // Returning true signals dalloc handled it; we'll return true.
    return true;
}

static extent_hooks_t my_hooks = {
    .alloc = my_extent_alloc,
    .dalloc = my_extent_dalloc,
    .commit = nullptr,
    .decommit = nullptr,
    .purge_lazy = nullptr,
    .purge_forced = nullptr,
    .split = nullptr,
    .merge = nullptr
};

constexpr size_t POOL_SIZE = 1 << 30;
alignas(4096) static char buffer[POOL_SIZE];

static void jemalloc_pool_test() {
    ExtentPool pool(buffer, POOL_SIZE);
    g_extent_pool = &pool;

    int ret;

    // Create new arena
    size_t sz = sizeof(my_arena_index);
    if ((ret = mallctl("arenas.create", &my_arena_index, &sz, nullptr, 0)))
        LOG_ERROR("mallctl arena.create returned " << strerror(ret))
    // Assign hooks
    extent_hooks_t* new_hooks = &my_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    if ((ret = mallctl(("arena." + std::to_string(my_arena_index) + ".extent_hooks").c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks))))
        LOG_ERROR("mallctl arena.extent_hooks returned " << strerror(ret))

    if ((ret = mallctl("thread.arena", nullptr, nullptr, &my_arena_index, sizeof(my_arena_index))))
        LOG_ERROR("mallctl thread.arena returned " << strerror(ret))
    // now allocate a tiny block

    // Allocate using custom arena
    void* p1 = mallocx(32, MALLOCX_ARENA(my_arena_index) | MALLOCX_TCACHE_NONE);
    void* p2 = mallocx(100, MALLOCX_ARENA(my_arena_index));
    void* p3 = mallocx(200, MALLOCX_ARENA(my_arena_index));
    std::cout << "Allocated p1=" << p1 << ", p2=" << p2 << ", p3=" << p3 << "\n";

    dallocx(p1, MALLOCX_ARENA(my_arena_index));
    dallocx(p2, MALLOCX_ARENA(my_arena_index));
    dallocx(p3, MALLOCX_ARENA(my_arena_index));
    std::cout << "Freed p1, p2, p3\n";
}

#endif
