#ifndef _CXL_MALLOC_H_
#define _CXL_MALLOC_H_

#include "memoryPool.hpp"

extern MemoryPool<64, 128, 256> cxlhc_pool;

inline void hc_initialize(void *buf, size_t size) {
    cxlhc_pool.initialize(buf, size);
}

inline void *hc_malloc(size_t size) {
    return cxlhc_pool.allocate(size);
}

inline void hc_free(void *ptr, size_t size) {
    return cxlhc_pool.deallocate(ptr, size);
}

#endif
