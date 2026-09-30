#ifndef _USER_H_
#define _USER_H_

#include "stdint.h"
#include "flushUtils.hpp"
#include "cxlMalloc.hpp"
#include "cxlSync.hpp"
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

void rac_init(unsigned nid, size_t cxl_hc_range, size_t cxl_nhc_range, size_t root_size);

void rac_shutdown();

unsigned rac_get_node_id();

unsigned rac_get_node_count();

void* rac_get_user_root();

CXLBarrier *rac_get_root_barrier();

// no threads on the current node can access SWC memory when calling rac_subscribe_to_node
void rac_subscribe_to_node(unsigned target);

void rac_unsubscribe_from_node(unsigned target);

bool rac_is_subscribed_to_node(unsigned target);

int rac_thread_create(unsigned nid, pthread_t *thread, void *(*func)(void*), void *arg);

int rac_thread_join(unsigned nid, pthread_t thread, void **thread_ret);

inline bool in_cxl_nhc_mem(void *addr) {
    //return (addr >= cxl_nhc_buf) & (addr < (cxl_nhc_buf + cxl_nhc_range));
    return ((uintptr_t)addr >= CXL_NHC_START);
}

inline void rac_post_writeback(void *begin, void *end) {
#if PROTOCOL_OFF || EAGER_WRITEBACK
    if (in_cxl_nhc_mem((char*)begin))
        do_range_writeback((char *)begin, (char *)end - (char *)begin);
#endif
#if !PROTOCOL_OFF
    if (in_cxl_nhc_mem((char*)begin))
        thread_ops->log_range_store((char *)begin, (char *)end);
#endif
}

//invalidate part of dst that partially covers cache lines
inline void invalidate_boundaries(char *begin, char *end) {
    uintptr_t bptr = (uintptr_t) begin;
    uintptr_t eptr = (uintptr_t) end;
    if (bptr & CACHE_LINE_MASK)
#if PROTOCOL_OFF
        do_invalidate(begin);
#else
        check_invalidate(begin);
#endif
    if (eptr & CACHE_LINE_MASK &&
        (bptr & CACHE_LINE_MASK) != (eptr & CACHE_LINE_MASK))
#if PROTOCOL_OFF
        do_invalidate(end);
#else
        check_invalidate(begin);
#endif
}

inline void rac_store_pre_invalidate(void *begin, void *end) {
#if PROTOCOL_OFF || !EAGER_INVALIDATE
    if (in_cxl_nhc_mem((char*)begin))
        invalidate_boundaries((char*)begin, (char*)end);
#endif
}

inline void rac_load_pre_invalidate(void *begin, void *end) {
#if PROTOCOL_OFF
    if (in_cxl_nhc_mem((char*)begin))
        do_range_invalidate((char*)begin, (char*)end-(char*)begin);
#endif
#if !EAGER_INVALIDATE
    if (in_cxl_nhc_mem((char*)begin))
        check_range_invalidate((char*)begin, (char*)end);
#endif
}

} // RACoherence

using namespace RACoherence;

#if PROTOCOL_OFF

#define DO_INVALIDATE(x) do_invalidate(x)
#define INVALIDATE_FENCE() invalidate_fence()
#define DO_WRITEBACK(x) do_writeback(x)
#define CHECK_INVALIDATE(x) do {} while(0)
#define LOG_STORE(x) do {} while(0)

#else

#define DO_INVALIDATE(x) do {} while(0)
#define INVALIDATE_FENCE() do {} while(0)
#define DO_WRITEBACK(x) do {} while(0)

#if EAGER_INVALIDATE
#define CHECK_INVALIDATE(x) do {} while(0)
#else
#define CHECK_INVALIDATE(x) check_invalidate(x)
#endif

#define LOG_STORE(x) thread_ops->log_store(x)

#endif

// extern "C" APIs to be inserted by compiler instrumentation. Do not use directly.
#define RACLOAD(size) \
    inline __attribute__((used)) uint ## size ## _t rac_load ## size(void * addr, const char * /*position*/) { \
        if (in_cxl_nhc_mem(addr)) { \
            DO_INVALIDATE((char *)addr); \
            INVALIDATE_FENCE(); \
            CHECK_INVALIDATE((char *)addr); \
        } \
        return *((uint ## size ## _t*)addr); \
    }

#define RACSTORE(size) \
    inline __attribute__((used)) void rac_store ## size(void * addr, uint ## size ## _t val, const char * /*position*/) {  \
        bool in_cxl_nhc = in_cxl_nhc_mem(addr); \
        if (in_cxl_nhc) { \
            DO_INVALIDATE((char *)addr); \
            INVALIDATE_FENCE(); \
            CHECK_INVALIDATE((char *)addr); \
            LOG_STORE((char *)addr); \
        } \
        *((uint ## size ## _t*)addr) = val; \
        if (in_cxl_nhc) \
            DO_WRITEBACK((char *)addr); \
    }

RACSTORE(8)
RACSTORE(16)
RACSTORE(32)
RACSTORE(64)

RACLOAD(8)
RACLOAD(16)
RACLOAD(32)
RACLOAD(64)

#endif
