#ifndef _CXL_MALLOC_H_
#define _CXL_MALLOC_H_

#include "config.hpp"
#include "jemallocPool.hpp"

#ifdef HC_USE_DLMALLOC
#include "dlmalloc.h"
extern mspace cxlhc_space;
#else
#include "memoryPool.hpp"
using CXLHCPool = MemoryPool<64, 128>;
extern CXLHCPool *cxlhc_pool;
#endif
extern ExtentPool *cxlnhc_extent_pool;

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
    if (!cxlnhc_extent_pool) return true; // claim we handled it
    cxlnhc_extent_pool->dealloc_extent(addr, size);
    return true;
}

extern extent_hooks_t cxlnhc_hooks;

inline void cxlnhc_pool_initialize(char *hc_buf, size_t &hc_off, char *buf, size_t size) {
    cxlnhc_extent_pool = new (hc_buf + hc_off) ExtentPool(buf, size);
    hc_off+= sizeof(cxlnhc_extent_pool);
    extent_hooks_t* new_hooks = &cxlnhc_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    int ret;
    if ((ret = mallctl("arena.0.extent_hooks", &old_hooks, &olen, &new_hooks, sizeof(new_hooks))))
        LOG_ERROR("mallctl arena.extent_hooks returned " << strerror(ret))
}

inline void cxlhc_pool_initialize(char *buf, size_t size) {
#ifdef HC_USE_DLMALLOC
    cxlhc_space = create_mspace_with_base(buf, size, true);
#else
    assert(size > sizeof(CXLHCPool));
    cxlhc_pool = new (buf) CXLHCPool(buf + sizeof(CXLHCPool), size - sizeof(CXLHCPool));
#endif
}

inline void *cxlhc_malloc(size_t size) {
#ifdef HC_USE_DLMALLOC
    return mspace_malloc(cxlhc_space, size);
#else
    return cxlhc_pool->allocate(size);
#endif
}

inline void cxlhc_free(void *ptr, size_t size) {
#ifdef HC_USE_DLMALLOC
    return mspace_free(cxlhc_space, ptr);
#else
    return cxlhc_pool->deallocate(ptr, size);
#endif
}

#endif
