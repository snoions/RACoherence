#ifndef _USER_H_
#define _USER_H_

#include "stdint.h"
#include "flushUtils.hpp"
#include "cxlMalloc.hpp"
#include "threadOps.hpp"

#if __cplusplus
extern "C" {
#endif
// instrumentation functions
uint8_t rac_load8(void *addr, const char *);
uint16_t rac_load16(void *addr, const char *);
uint32_t rac_load32(void *addr, const char *);
uint64_t rac_load64(void *addr, const char *);

void rac_store8(void * addr, uint8_t val, const char *);
void rac_store16(void * addr, uint16_t val, const char *);
void rac_store32(void * addr, uint32_t val, const char *);
void rac_store64(void * addr, uint64_t val, const char *);
#if __cplusplus
}
#endif

namespace RACoherence {

extern char *cxl_nhc_buf;
extern size_t cxl_nhc_range;

void rac_init(unsigned nid, size_t cxl_hc_range, size_t cxl_nhc_range);

void rac_shutdown();

unsigned rac_get_node_id();

unsigned rac_get_node_count();

int rac_thread_create(unsigned nid, pthread_t *thread, void *(*func)(void*), void *arg);

int rac_thread_join(unsigned nid, pthread_t thread, void **thread_ret);

inline bool in_cxl_nhc_mem(void *addr) {
    //return (addr >= cxl_nhc_buf) & (addr < (cxl_nhc_buf + cxl_nhc_range));
    return ((uintptr_t)addr & CXL_NHC_START);
}

inline void rac_post_flush(void *begin, void *end) {
#ifdef PROTOCOL_OFF
    if (in_cxl_nhc_mem((char*)begin))
        do_range_flush((char *)begin, (char *)end - (char *)begin);
#else
    if (in_cxl_nhc_mem((char*)begin))
        thread_ops->log_range_store((char *)begin, (char *)end);
#endif
}

inline void rac_store_pre_invalidate(void *begin, void *end) {
#ifdef PROTOCOL_OFF
    if (in_cxl_nhc_mem((char*)begin))
        invalidate_boundaries((char*)begin, (char*)end);
#elif !defined(EAGER_INVALIDATE)
    if (in_cxl_nhc_mem((char*)begin))
        invalidate_boundaries((char*)begin, (char*)end);
#endif
}

} // RACoherence

using namespace RACoherence;

//TODO: handle rare unaligned accesses for sizes larger than 8
#ifdef PROTOCOL_OFF
#define RACLOAD(size) \
    inline __attribute__((used)) uint ## size ## _t rac_load ## size(void * addr, const char * /*position*/) { \
        if (in_cxl_nhc_mem(addr)) { \
            do_invalidate((char *)addr); \
            invalidate_fence(); \
        } \
        return *((uint ## size ## _t*)addr); \
    }
#elif defined(EAGER_INVALIDATE)
#define RACLOAD(size) \
    inline __attribute__((used)) uint ## size ## _t rac_load ## size(void * addr, const char * /*position*/) { \
        return *((uint ## size ## _t*)addr); \
    }
#else
#define RACLOAD(size) \
    inline __attribute__((used)) uint ## size ## _t rac_load ## size(void * addr, const char * /*position*/) { \
        if (in_cxl_nhc_mem(addr)) { \
            check_invalidate((char *)addr); \
        } \
        return *((uint ## size ## _t*)addr); \
    }
#endif

#ifdef PROTOCOL_OFF
#define RACSTORE(size) \
    inline __attribute__((used)) void rac_store ## size(void * addr, uint ## size ## _t val, const char * /*position*/) {  \
        bool in_cxl_nhc = in_cxl_nhc_mem(addr); \
        if (in_cxl_nhc) { \
            do_invalidate((char *)addr); \
            invalidate_fence(); \
        } \
        *((uint ## size ## _t*)addr) = val; \
        if (in_cxl_nhc) \
            do_flush((char *)addr); \
    }
#elif defined(EAGER_INVALIDATE)
#define RACSTORE(size) \
    inline __attribute__((used)) void rac_store ## size(void * addr, uint ## size ## _t val, const char * /*position*/) {  \
        if (in_cxl_nhc_mem(addr)) { \
            thread_ops->log_store((char *)addr); \
        } \
        *((uint ## size ## _t*)addr) = val; \
    }
#else 
#define RACSTORE(size) \
    inline __attribute__((used)) void rac_store ## size(void * addr, uint ## size ## _t val, const char * /*position*/) {  \
        if (in_cxl_nhc_mem(addr)) { \
            check_invalidate((char *)addr); \
            thread_ops->log_store((char *)addr); \
        } \
        *((uint ## size ## _t*)addr) = val; \
    }
#endif

RACSTORE(8)
RACSTORE(16)
RACSTORE(32)
RACSTORE(64)

RACLOAD(8)
RACLOAD(16)
RACLOAD(32)
RACLOAD(64)

#endif
