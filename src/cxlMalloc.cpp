#include <sstream>
#include "config.hpp"
#include "cxlMalloc.hpp"
#include "slabPool.hpp"
#include "extentPool.hpp"

namespace RACoherence {

#ifdef HC_USE_CUSTOM_POOL
using CXLHCPool = SlabPool<8, 128>;
CXLHCPool *cxlhc_pool;
#else
unsigned cxlhc_arena_index;
ExtentPool *cxlhc_extent_pool;
#endif
unsigned cxlnhc_arena_index;
ExtentPool *cxlnhc_extent_pool;

} // RACoherence

using namespace RACoherence;

const char *je_malloc_conf ="narenas:1";

#ifdef HC_USE_CUSTOM_POOL
void cxlhc_pool_init(char *buf, size_t size) {
    assert(size > sizeof(CXLHCPool));
    cxlhc_pool = new (buf) CXLHCPool(buf + sizeof(CXLHCPool), size - sizeof(CXLHCPool));
}

void *cxlhc_malloc(size_t size) {
    return cxlhc_pool->allocate(size);
}

void cxlhc_free(void *ptr, size_t size) {
    return cxlhc_pool->deallocate(ptr, size);
}
#else

inline void* cxlhc_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    if (!cxlhc_extent_pool) return nullptr;

    void* p = cxlhc_extent_pool->allocate(size, alignment);
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
    // if (!cxlhc_extent_pool) return true;
    // cxlhc_extent_pool->deallocate(addr, size);
    // return false;
    return true;
}

bool cxlhc_extent_split(extent_hooks_t *extent_hooks, void *addr, size_t size,
                     size_t size_a, size_t size_b, bool committed, unsigned arena_ind) {
    return false; 
}

bool cxlhc_extent_merge(extent_hooks_t *extent_hooks, void *addr_a, size_t size_a,
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
    .split         = cxlhc_extent_split,
    .merge         = cxlhc_extent_merge,
};

void cxlhc_pool_init(char *buf, size_t size) {
    int ret;
    //size_t sz = sizeof(cxlhc_arena_index);
    //if ((ret = je_mallctl("arenas.create", &cxlhc_arena_index, &sz, nullptr, 0))) {
    //    LOG_ERROR("je_mallctl arena.create returned " << strerror(ret))
    //    std::exit(EXIT_FAILURE);
    //}
    // make default arena use cxlhc memory
    cxlhc_arena_index = 0;
    cxlhc_extent_pool = new (buf) ExtentPool(buf + sizeof(ExtentPool), size - sizeof(ExtentPool));
    extent_hooks_t* new_hooks = &cxlhc_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    std::stringstream ss;
    ss << "arena." << cxlhc_arena_index << ".extent_hooks";
    if ((ret = je_mallctl(ss.str().c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks)))) {
        LOG_ERROR("je_mallctl " << ss.str() << " returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
}

void *cxlhc_malloc(size_t size) {
    return je_mallocx(size, MALLOCX_ARENA(cxlhc_arena_index) | MALLOCX_TCACHE_NONE);
}

void cxlhc_free(void *ptr, size_t size) {
    je_dallocx(ptr, MALLOCX_ARENA(cxlhc_arena_index) | MALLOCX_TCACHE_NONE);
}
#endif

inline void* cxlnhc_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    if (!cxlnhc_extent_pool) return nullptr;

    void* p = cxlnhc_extent_pool->allocate(size, alignment);
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
    // if (!cxlnhc_extent_pool) return true;
    // cxlnhc_extent_pool->deallocate(addr, size);
    // return false;
    return true;
}

bool cxlnhc_extent_split(extent_hooks_t *extent_hooks, void *addr, size_t size,
                     size_t size_a, size_t size_b, bool committed, unsigned arena_ind) {
    return false; 
}

bool cxlnhc_extent_merge(extent_hooks_t *extent_hooks, void *addr_a, size_t size_a,
                     void *addr_b, size_t size_b, bool committed, unsigned arena_ind) {
    return false;
}

extent_hooks_t cxlnhc_hooks = {
    .alloc         = cxlnhc_extent_alloc,
    .dalloc        = cxlnhc_extent_dalloc,
    .destroy       = nullptr,
    .commit        = nullptr,
    .decommit      = nullptr,
    .purge_lazy    = nullptr,
    .purge_forced  = nullptr,
    .split         = cxlnhc_extent_split,
    .merge         = cxlnhc_extent_merge,
};

void cxlnhc_pool_init(char *hc_buf, char *buf, size_t size) {
    int ret;
    size_t sz = sizeof(cxlnhc_arena_index);
    if ((ret = je_mallctl("arenas.create", &cxlnhc_arena_index, &sz, nullptr, 0))) {
        LOG_ERROR("je_mallctl arena.create returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
    cxlnhc_extent_pool = new (hc_buf) ExtentPool(buf, size);
    extent_hooks_t* new_hooks = &cxlnhc_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    std::stringstream ss;
    ss << "arena." << cxlnhc_arena_index << ".extent_hooks";
    if ((ret = je_mallctl(ss.str().c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks)))) {
        LOG_ERROR("je_mallctl arena.extent_hooks returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
    // clear extent pool from default arena
    if ((ret = je_mallctl("arena.0.purge", NULL, NULL, NULL, 0))) {
        LOG_ERROR("je_mallctl arena.0.purge returned " << strerror(ret))
        std::exit(EXIT_FAILURE);
    }
    cxlnhc_thread_init();
}

void cxlnhc_thread_init() {
    int ret;
    ret = je_mallctl("thread.arena", NULL, NULL, &cxlnhc_arena_index, sizeof(cxlnhc_arena_index));
    if (ret)
        LOG_ERROR("je_mallctl thread.arena returned " << strerror(ret))
    bool enabled = true;
    ret = je_mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);
    if (ret)
        LOG_ERROR("je_mallctl thread.tcache.flush returned " << strerror(ret))
}

void *cxlnhc_malloc(size_t size) {
    return je_mallocx(size, MALLOCX_ARENA(cxlnhc_arena_index));
}

void *cxlnhc_cl_aligned_malloc(size_t size) {
    return je_mallocx(size,  MALLOCX_ARENA(cxlnhc_arena_index) | MALLOCX_ALIGN(CACHE_LINE_SIZE));
}

void cxlnhc_free(void *ptr, size_t size) {
    je_dallocx(ptr, MALLOCX_ARENA(cxlnhc_arena_index));
}
