#include <sstream>
#include "config.hpp"
#include "cxlMalloc.hpp"
#include "global.hpp"
#include "extentPool.hpp"
#include "slabPool.hpp"

namespace RACoherence {

extern RACGlobal *global;
unsigned cxlhc_arena_index;
unsigned cxlnhc_arena_index;

inline void* cxlhc_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    if (!global) return nullptr;

    void* p = global->cxlhc_pool.allocate(size, alignment);
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
    if (!global) return nullptr;

    void* p = global->cxlnhc_pool.allocate(size, alignment);
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

void cxl_pool_init() {
    int ret;
    size_t sz = sizeof(unsigned);
    extent_hooks_t* new_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
#ifndef HC_USE_CUSTOM_POOL
    if ((ret = je_mallctl("arenas.create", &cxlhc_arena_index, &sz, nullptr, 0))) {
        LOG_ERROR("je_mallctl arena.create returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }

    new_hooks = &cxlhc_hooks;
    std::stringstream ss;
    ss << "arena." << cxlhc_arena_index << ".extent_hooks";
    if ((ret = je_mallctl(ss.str().c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks)))) {
        LOG_ERROR("je_mallctl " << ss.str() << " returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
#endif
    if ((ret = je_mallctl("arenas.create", &cxlnhc_arena_index, &sz, nullptr, 0))) {
        LOG_ERROR("je_mallctl arena.create returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }

    new_hooks = &cxlnhc_hooks;
    std::stringstream ss2;
    ss2 << "arena." << cxlnhc_arena_index << ".extent_hooks";
    if ((ret = je_mallctl(ss2.str().c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks)))) {
        LOG_ERROR("je_mallctl arena.extent_hooks returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
}

void cxl_pool_thread_init() {
    int ret;
#ifdef HC_USE_CUSTOM_POOL
    ret = je_mallctl("thread.arena", NULL, NULL, &cxlnhc_arena_index, sizeof(cxlnhc_arena_index));
#else
    ret = je_mallctl("thread.arena", NULL, NULL, &cxlhc_arena_index, sizeof(cxlhc_arena_index));
#endif
    if (ret)
        LOG_ERROR("je_mallctl thread.arena returned " << strerror(ret))
    bool enabled = true;
    ret = je_mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);
    if (ret)
        LOG_ERROR("je_mallctl thread.tcache.flush returned " << strerror(ret))
}

void print_jemalloc_stats() {
    // Refresh epoch to get current stats
    uint64_t epoch = 1;
    je_mallctl("epoch", NULL, NULL, &epoch, sizeof(epoch)); 
    je_malloc_stats_print(NULL, NULL, NULL);
}

} // RACoherence

using namespace RACoherence;

const char *je_malloc_conf ="narenas:1"; //,retain:false";

#ifdef HC_USE_CUSTOM_POOL
void *cxlhc_malloc(size_t size) {
    return global->cxlhc_pool.allocate(size);
}

void cxlhc_free(void *ptr, size_t size) {
    return global->cxlhc_pool.deallocate(ptr, size);
}
#else

void *cxlhc_malloc(size_t size) {
    return je_mallocx(size, MALLOCX_ARENA(cxlhc_arena_index));
}

void cxlhc_free(void *ptr, size_t size) {
    je_dallocx(ptr, MALLOCX_ARENA(cxlhc_arena_index));
}
#endif

void *cxlnhc_malloc(size_t size) {
    return je_mallocx(size, MALLOCX_ARENA(cxlnhc_arena_index) | MALLOCX_TCACHE_NONE);
}

void *cxlnhc_cl_aligned_malloc(size_t size) {
    return je_mallocx(size,  MALLOCX_ARENA(cxlnhc_arena_index) | MALLOCX_ALIGN(CACHE_LINE_SIZE) | MALLOCX_TCACHE_NONE);
}

void cxlnhc_free(void *ptr, size_t size) {
    je_dallocx(ptr, MALLOCX_ARENA(cxlnhc_arena_index) | MALLOCX_TCACHE_NONE);
}
