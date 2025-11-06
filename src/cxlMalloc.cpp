#include <sstream>

#include "config.hpp"
#include "cxlMalloc.hpp"
#include "memoryPool.hpp"
#include "jemallocPool.hpp"

#ifdef HC_USE_DLMALLOC
#include "dlmalloc.h"
mspace cxlhc_space;
#else

namespace RACoherence {

//using CXLHCPool = MemoryPool<8, 128, 256>;
using CXLHCPool = MemoryPool<8, 128>;
CXLHCPool *cxlhc_pool;
#endif
unsigned cxlnhc_arena_index;
ExtentPool *cxlnhc_extent_pool;

} // RACoherence

using namespace RACoherence;

inline void* cxlnhc_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    if (!cxlnhc_extent_pool) return nullptr;

    void* p = cxlnhc_extent_pool->alloc_extent(size, alignment);
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
    if (!cxlnhc_extent_pool) return true;
    cxlnhc_extent_pool->dealloc_extent(addr, size);
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
    .split         = nullptr,
    .merge         = nullptr
};

void cxlnhc_pool_init(char *hc_buf, char *buf, size_t size) {
    int ret;
    size_t sz = sizeof(cxlnhc_arena_index);
    if ((ret = je_mallctl("arenas.create", &cxlnhc_arena_index, &sz, nullptr, 0)))
        LOG_ERROR("je_mallctl arena.create returned " << strerror(ret))
    cxlnhc_extent_pool = new (hc_buf) ExtentPool(buf, size);
    extent_hooks_t* new_hooks = &cxlnhc_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    std::stringstream ss;
    ss << "arena." << cxlnhc_arena_index << ".extent_hooks";
    if ((ret = je_mallctl(ss.str().c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks)))) {
        LOG_ERROR("je_mallctl arena.extent_hooks returned " << strerror(ret))
    }
    cxlnhc_thread_init();
}

void cxlnhc_thread_init() {
    if (int ret = je_mallctl("thread.arena", NULL, NULL, &cxlnhc_arena_index, sizeof(cxlnhc_arena_index)) != 0)
        LOG_ERROR("je_mallctl thread.arena returned " << strerror(ret))
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

void cxlhc_pool_init(char *buf, size_t size) {
#ifdef HC_USE_DLMALLOC
    cxlhc_space = create_mspace_with_base(buf, size, true);
#else
    assert(size > sizeof(CXLHCPool));
    cxlhc_pool = new (buf) CXLHCPool(buf + sizeof(CXLHCPool), size - sizeof(CXLHCPool));
#endif
}

void *cxlhc_malloc(size_t size) {
#ifdef HC_USE_DLMALLOC
    return mspace_malloc(cxlhc_space, size);
#else
    return cxlhc_pool->allocate(size);
#endif
}

void cxlhc_free(void *ptr, size_t size) {
#ifdef HC_USE_DLMALLOC
    return mspace_free(cxlhc_space, ptr);
#else
    return cxlhc_pool->deallocate(ptr, size);
#endif
}
