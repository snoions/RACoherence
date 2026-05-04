#include <sstream>
#include "config.hpp"
#include "cxlMalloc.hpp"
#include "globalMeta.hpp"
#include "mimalloc.h"
#include "jemalloc/jemalloc.h"

const char *je_malloc_conf ="narenas:1"; //,retain:false";

namespace RACoherence {

AllocMeta *alloc_meta;
#ifdef USE_GLOBAL_MIMALLOC
mi_arena_id_t hc_arena;
mi_arena_id_t nhc_arena;
__thread mi_heap_t* hc_heap;
__thread mi_heap_t* nhc_heap;
#else
unsigned hc_arena_index;
unsigned nhc_arena_index;
#endif

#ifndef USE_GLOBAL_MIMALLOC
inline void* cxlhc_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    if (!alloc_meta) return nullptr;

    void* p = alloc_meta->cxlhc_pool.allocate(size, alignment);
    if (!p) {
        return nullptr;
    }

    if (zero && *zero) {
        std::memset(p, 0, size);
        *zero = false; // we satisfied zero
    }
    if (commit) *commit = true;
    return p;
}

inline bool cxlhc_extent_dalloc(extent_hooks_t* /*hooks*/,
                             void* addr, size_t size, bool /*committed*/, unsigned /*arena_ind*/) {
    // let jemalloc keep the memory
    return true;
}

bool extent_split(extent_hooks_t *extent_hooks, void *addr, size_t size,
                     size_t size_a, size_t size_b, bool committed, unsigned arena_ind) {
    return false; 
}

bool extent_merge(extent_hooks_t *extent_hooks, void *addr_a, size_t size_a,
                     void *addr_b, size_t size_b, bool committed, unsigned arena_ind) {
    return false;
}

extent_hooks_t cxlhc_hooks = {
    .alloc         = cxlhc_extent_alloc,
    .dalloc        = cxlhc_extent_dalloc,
    .destroy       = nullptr,
    .commit        = nullptr,
    .decommit      = nullptr,
    .purge_lazy    = nullptr,
    .purge_forced  = nullptr,
    .split         = extent_split,
    .merge         = extent_merge,
};

inline void* cxlnhc_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    if (!alloc_meta) return nullptr;

    void* p = alloc_meta->cxlnhc_pool.allocate(size, alignment);
    if (!p) {
        return nullptr;
    }

    if (zero && *zero) {
        std::memset(p, 0, size);
        *zero = false; // we satisfied zero
    }
    if (commit) *commit = true;
    return p;
}

inline bool cxlnhc_extent_dalloc(extent_hooks_t* /*hooks*/,
                             void* addr, size_t size, bool /*committed*/, unsigned /*arena_ind*/) {

    // let jemalloc keep the memory
    return true;
}

extent_hooks_t cxlnhc_hooks = {
    .alloc         = cxlnhc_extent_alloc,
    .dalloc        = cxlnhc_extent_dalloc,
    .destroy       = nullptr,
    .commit        = nullptr,
    .decommit      = nullptr,
    .purge_lazy    = nullptr,
    .purge_forced  = nullptr,
    .split         = extent_split,
    .merge         = extent_merge,
};

void print_jemalloc_stats() {
    // Refresh epoch to get current stats
    uint64_t epoch = 1;
    je_mallctl("epoch", NULL, NULL, &epoch, sizeof(epoch));
    je_malloc_stats_print(NULL, NULL, NULL);
}
#endif

void cxl_alloc_process_init(AllocMeta *a_meta, char *hc_buf, size_t hc_range, char *nhc_buf, size_t nhc_range, bool is_first) { 
    //align start of hc pool to cache line
    alloc_meta = a_meta;
#ifdef USE_GLOBAL_MIMALLOC
    mi_option_set(mi_option_purge_delay, -1);
    size_t nhc_meta_size = mi_cxl_meta_region_size(nhc_range);
    size_t hc_meta_size = mi_cxl_meta_region_size(hc_range - nhc_meta_size);
    assert(hc_range > nhc_meta_size + hc_meta_size && "hardware coherent region too small");

    if (!mi_manage_os_memory_shared_disjoint(hc_buf, hc_meta_size, hc_buf + hc_meta_size, hc_range - hc_meta_size - nhc_meta_size, &hc_arena)) {
        LOG_ERROR("mi_manage_os_memory_shared_disjoint(hc_pool) failed")
        std::exit(EXIT_FAILURE);
    }

    if (!mi_manage_os_memory_shared_disjoint(hc_buf + hc_range - nhc_meta_size, nhc_meta_size, nhc_buf, nhc_range, &nhc_arena)) {
        LOG_ERROR("mi_manage_os_memory_shared_disjoint(nhc_pool) failed")
        std::exit(EXIT_FAILURE);
    }
#else
    if (size_t padding = CACHE_LINE_SIZE - (size_t)(hc_buf) & (CACHE_LINE_SIZE-1)) {
        hc_buf += padding;
        hc_range -= padding;
    }
    if (is_first) {
        new (&a_meta->cxlhc_pool) ExtentPool(hc_buf, hc_range);
        new (&a_meta->cxlnhc_pool) ExtentPool(nhc_buf, nhc_range);
    }

    int ret;
    size_t sz = sizeof(unsigned);
    extent_hooks_t* new_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    if ((ret = je_mallctl("arenas.create", &hc_arena_index, &sz, nullptr, 0))) {
        LOG_ERROR("je_mallctl arena.create returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }

    new_hooks = &cxlhc_hooks;
    std::stringstream ss;
    ss << "arena." << hc_arena_index << ".extent_hooks";
    if ((ret = je_mallctl(ss.str().c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks)))) {
        LOG_ERROR("je_mallctl " << ss.str() << " returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
    if ((ret = je_mallctl("arenas.create", &nhc_arena_index, &sz, nullptr, 0))) {
        LOG_ERROR("je_mallctl arena.create returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }

    new_hooks = &cxlnhc_hooks;
    std::stringstream ss2;
    ss2 << "arena." << nhc_arena_index << ".extent_hooks";
    if ((ret = je_mallctl(ss2.str().c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks)))) {
        LOG_ERROR("je_mallctl arena.extent_hooks returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
#endif
}

void cxl_alloc_thread_init() {
#ifdef USE_GLOBAL_MIMALLOC
    hc_heap = mi_heap_new_in_arena(hc_arena);
    nhc_heap = mi_heap_new_in_arena(nhc_arena);
#else
    int ret;
    ret = je_mallctl("thread.arena", NULL, NULL, &hc_arena_index, sizeof(hc_arena_index));
    if (ret)
        LOG_ERROR("je_mallctl thread.arena returned " << strerror(ret))
    ret = je_mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);
    if (ret)
        LOG_ERROR("je_mallctl thread.tcache.flush returned " << strerror(ret))
#endif
}

void cxl_alloc_thread_exit() {
#ifdef USE_GLOBAL_MIMALLOC
    mi_heap_collect(hc_heap, true);
    mi_heap_delete(hc_heap);
    mi_heap_collect(nhc_heap, true);
    mi_heap_delete(nhc_heap);
    mi_collect(true);
#endif
}

} // RACoherence

using namespace RACoherence;

void *cxlhc_malloc(size_t size) {
#ifdef USE_GLOBAL_MIMALLOC
    return mi_heap_malloc(hc_heap, size);
#else 
    return je_mallocx(size, MALLOCX_ARENA(hc_arena_index));
#endif
}

void cxlhc_free(void *ptr, size_t size) {
#ifdef USE_GLOBAL_MIMALLOC
    mi_free(ptr);
#else
    je_dallocx(ptr, MALLOCX_ARENA(hc_arena_index));
#endif
}

void *cxlnhc_malloc(size_t size) {
#ifdef USE_GLOBAL_MIMALLOC
    return mi_heap_malloc(nhc_heap, size);
#else
    return je_mallocx(size, MALLOCX_ARENA(nhc_arena_index) | MALLOCX_TCACHE_NONE);
#endif
}

void *cxlnhc_cl_aligned_malloc(size_t size) {
#ifdef USE_GLOBAL_MIMALLOC
    return mi_heap_malloc_aligned(nhc_heap, size, CACHE_LINE_SIZE);
#else
    return je_mallocx(size,  MALLOCX_ARENA(nhc_arena_index) | MALLOCX_ALIGN(CACHE_LINE_SIZE) | MALLOCX_TCACHE_NONE);
#endif
}

void cxlnhc_free(void *ptr, size_t size) {
#ifdef USE_GLOBAL_MIMALLOC
    mi_free(ptr);
#else
    je_dallocx(ptr, MALLOCX_ARENA(nhc_arena_index) | MALLOCX_TCACHE_NONE);
#endif
}
