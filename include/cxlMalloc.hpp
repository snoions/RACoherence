#ifndef _CXL_MALLOC_H_
#define _CXL_MALLOC_H_


#ifdef HC_USE_DLMALLOC
#include "dlmalloc.h"
extern mspace cxlhc_space;
#else
#include "memoryPool.hpp"
using CXLHCPool = MemoryPool<64, 128>;
extern CXLHCPool *cxlhc_pool;
#endif

void cxlnhc_pool_initialize(char *hc_buf, size_t &hc_off, char *buf, size_t size);

void *cxlnhc_malloc(size_t size);

void cxlnhc_free(void *ptr, size_t size);

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
