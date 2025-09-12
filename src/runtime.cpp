#include <atomic>
#include <dlfcn.h>
#include <pthread.h>
#include "cacheAgent.hpp"
#include "cxlSync.hpp"
#include "flushUtils.hpp"
#include "logger.hpp"
#include "numaUtils.hpp"
#include "jemallocPool.hpp"
#include "runtime.hpp"

thread_local ThreadOps *thread_ops;
std::atomic<bool> complete {false};
std::atomic<unsigned> curr_tid {0};
pthread_t cacheAgent_group[NODE_COUNT];
char *cxl_nhc_buf;
char *cxl_hc_buf;
char *node_local_buf;
CacheInfo *cache_infos;
LogManager *log_mgrs;

inline bool in_cxl_nhc_mem(void *addr) {
    return addr >= cxl_nhc_buf && addr < cxl_nhc_buf + CXL_NHC_RANGE && cxl_nhc_buf;
}

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
        if (in_cxl_nhc_mem(addr)) { \
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

struct RACThreadArg {
    const VectorClock *parent_clock;
    unsigned nid;
    void* (*func)(void*);
    void* arg;
};

struct RACThreadRet {
    ThreadOps *ops;
    void* ret;
};

//TODO: only need a full thread_acquire/release if parent and child threads are on different nodes, only need to merge clocks
void *rac_thread_func_wrapper(void *arg) {
    auto rac_arg = (RACThreadArg *)arg;
    unsigned tid = curr_tid.fetch_add(1, std::memory_order_relaxed);
    thread_ops = new ThreadOps(log_mgrs, &cache_infos[rac_arg->nid], rac_arg->nid, tid);
    if (rac_arg->parent_clock)
        thread_ops->thread_acquire(*rac_arg->parent_clock);
    void* ret = rac_arg->func(rac_arg->arg);
#ifndef PROTOCOL_OFF
    thread_ops->thread_release();
#endif
    delete rac_arg;
    auto *rac_ret = new RACThreadRet{thread_ops, ret};
    return rac_ret;
}

int rac_thread_create(unsigned nid, pthread_t *thread, void *(*func)(void*), void *arg) {
#ifndef PROTOCOL_OFF
    const VectorClock *parent_clock = &thread_ops->thread_release();
#else
    const VectorClock *parent_clock = nullptr;
#endif
    RACThreadArg *rac_arg = new RACThreadArg{parent_clock, nid, func, arg};
    return pthread_create(thread, nullptr, rac_thread_func_wrapper, rac_arg);
};

int rac_thread_join(unsigned /*nid*/, pthread_t thread, void **thread_ret) {
    RACThreadRet *rac_ret;
    int ret = pthread_join(thread, (void **)&rac_ret);
    auto child_ops = rac_ret->ops;
#ifndef PROTOCOL_OFF
    thread_ops->thread_acquire(child_ops->get_clock());
#endif
    if (thread_ret)
        *thread_ret = rac_ret->ret;
    delete child_ops;
    delete rac_ret;
    return ret;
}

unsigned rac_get_node_id() {
    return thread_ops->get_node_id();
}

void * (*volatile memcpy_real)(void * dst, const void *src, size_t n) = nullptr;
void * (*volatile memmove_real)(void * dst, const void *src, size_t len) = nullptr;
void (*volatile bzero_real)(void * dst, size_t len) = nullptr;
void * (*volatile memset_real)(void * dst, int c, size_t len) = nullptr;
char * (*volatile strcpy_real)(char * dst, const char *src) = nullptr;

void init_memory_ops() {
    if (!memcpy_real) {
        memcpy_real = (void * (*)(void * dst, const void *src, size_t n)) 1;
        memcpy_real = (void * (*)(void * dst, const void *src, size_t n))dlsym(RTLD_NEXT, "memcpy");
    }
    if (!memmove_real) {
        memmove_real = (void * (*)(void * dst, const void *src, size_t n)) 1;
        memmove_real = (void * (*)(void * dst, const void *src, size_t n))dlsym(RTLD_NEXT, "memmove");
    }

    if (!memset_real) {
        memset_real = (void * (*)(void * dst, int c, size_t n)) 1;
        memset_real = (void * (*)(void * dst, int c, size_t n))dlsym(RTLD_NEXT, "memset");
    }

    if (!strcpy_real) {
        strcpy_real = (char * (*)(char * dst, const char *src)) 1;
        strcpy_real = (char * (*)(char * dst, const char *src))dlsym(RTLD_NEXT, "strcpy");
    }
    if (!bzero_real) {
        bzero_real = (void (*)(void * dst, size_t len)) 1;
        bzero_real = (void (*)(void * dst, size_t len))dlsym(RTLD_NEXT, "bzero");
    }
}

//invalidate part of dst that partially covers cache lines
inline void invalidate_boundaries(char *begin, char *end) {
    uintptr_t bptr = (uintptr_t) begin;
    uintptr_t eptr = (uintptr_t) end;
    if (bptr & CACHE_LINE_MASK)
#ifdef PROTOCOL_OFF
        do_invalidate(begin);
#else 
        check_invalidate(begin);
#endif
    if (eptr & CACHE_LINE_MASK &&
        (bptr >> CACHE_LINE_SHIFT) != (eptr >> CACHE_LINE_SHIFT))
#ifdef PROTOCOL_OFF
        do_invalidate(end);
#else 
        check_invalidate(begin);
#endif
}

void * memcpy(void * dst, const void * src, size_t n) {
    void *ret;
    bool is_in_cxl_nhc = in_cxl_nhc_mem((char *)src) || in_cxl_nhc_mem((char *)dst);
    char *dst_begin = (char *)dst;
    char *dst_end = dst_begin + n;
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc) {
        do_range_invalidate((char *)src, n);
        invalidate_boundaries(dst_begin, dst_end); 
    }
#elif !defined(EAGER_INVALIDATE)
    char *src_begin = (char *)src;
    char *src_end = src_begin + n;
    if (is_in_cxl_nhc) {
        check_range_invalidate(src_begin, src_end);
        invalidate_boundaries(dst_begin, dst_end); 
    }
#endif
    if (((uintptr_t)memcpy_real) < 2) {
        for(unsigned i=0;i<n;i++) {
            ((volatile char *)dst)[i] = ((char *)src)[i];
        }
        ret = dst;
    } else
        ret = memcpy_real(dst, src, n);
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc)
        thread_ops->log_range_store(dst_begin, dst_end);
#endif
    return ret;
}

void * memmove(void *dst, const void *src, size_t n) {
    void *ret;
    bool is_in_cxl_nhc = in_cxl_nhc_mem((char *)src) || in_cxl_nhc_mem((char *)dst);
    char *dst_begin = (char *)dst;
    char *dst_end = dst_begin + n;
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc) {
        do_range_invalidate((char *)src, n);
        invalidate_boundaries(dst_begin, dst_end); 
    }
#elif !defined(EAGER_INVALIDATE)
    char *src_begin = (char *)src;
    char *src_end = src_begin + n;
    if (is_in_cxl_nhc) {
        check_range_invalidate(src_begin, src_end);
        invalidate_boundaries(dst_begin, dst_end); 
    }
#endif
    if (((uintptr_t)memmove_real) < 2) {
        if (((uintptr_t)dst) < ((uintptr_t)src))
            for(unsigned i=0;i<n;i++) {
                ((volatile char *)dst)[i] = ((char *)src)[i];
            }
        else
            for(unsigned i=n;i!=0; ) {
                i--;
                ((volatile char *)dst)[i] = ((char *)src)[i];
            }
        ret = dst;
    } else
        ret = memmove_real(dst, src, n);
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc)
        thread_ops->log_range_store(dst_begin, dst_end);
#endif
    return ret;
}

void * memset(void *dst, int c, size_t n) {
    void *ret;
    bool is_in_cxl_nhc = in_cxl_nhc_mem((char *)dst);
    char *dst_begin = (char *)dst;
    char *dst_end = dst_begin + n;
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc) {
        invalidate_boundaries(dst_begin, dst_end); 
    }
#elif !defined(EAGER_INVALIDATE)
    if(is_in_cxl_nhc) {
        invalidate_boundaries(dst_begin, dst_end); 
    }
#endif
    if (((uintptr_t)memset_real) < 2) {
        for(unsigned i=0;i<n;i++) {
            ((volatile char *)dst)[i] = (char) c;
        }
        ret = dst;
    } else
        ret = memset_real(dst, c, n);
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc)
        thread_ops->log_range_store(dst_begin, dst_end);
#endif
    return ret;
}

void bzero(void *dst, size_t n) {
    void *ret;
    bool is_in_cxl_nhc = in_cxl_nhc_mem((char *)dst);
    char *dst_begin = (char *)dst;
    char *dst_end = dst_begin + n;
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc) {
        invalidate_boundaries(dst_begin, dst_end); 
    }
#elif !defined(EAGER_INVALIDATE)
    if(is_in_cxl_nhc) {
        invalidate_boundaries(dst_begin, dst_end); 
     }
#endif
    if (((uintptr_t)bzero_real) < 2) {
        for(size_t s=0;s<n;s++) {
            ((volatile char *)dst)[s] = 0;
        }
    } else
        bzero_real(dst, n);
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc)
        thread_ops->log_range_store(dst_begin, dst_end);
#endif
}

char * strcpy(char *dst, const char *src) {
    char *ret;
    bool is_in_cxl_nhc = in_cxl_nhc_mem((char *)src) || in_cxl_nhc_mem((char *)dst);
    size_t n = 0;
    size_t m = 0;
    // we cannot invalidate ahead-of-time because the length is unknown
#if defined(PROTOCOL_OFF) || !defined(EAGER_INVALIDATE)
    bool need_invalidate = true;
#else
    bool need_invalidate = false;
#endif
    if (((uintptr_t)strcpy_real) < 2 || need_invalidate) {
        while (true) {
#ifdef PROTOCOL_OFF
            if (is_in_cxl_nhc) {
                do_invalidate((char *)&src[n]);
            }
#elif !defined(EAGER_INVALIDATE)
            if (is_in_cxl_nhc) {
                check_invalidate((char *)&src[n]);
            }
#endif
            bool end = false;
            for(;((uintptr_t)&src[n] & CACHE_LINE_MASK); n++) {
                if (src[n] == '\0') {
                    end = true;
                    break;
                }
            }
#ifdef PROTOCOL_OFF
            if (is_in_cxl_nhc && (end || m==0) && ((uintptr_t)&dst[n] & CACHE_LINE_MASK)) {
                do_invalidate((char *)&dst[n]);
            }
#elif !defined(EAGER_INVALIDATE)
            if (is_in_cxl_nhc && (end || m==0) && ((uintptr_t)&dst[n] & CACHE_LINE_MASK)) {
                check_invalidate((char *)&dst[n]);
            }
#endif
            for (; m < n; m++)
                ((volatile char *)dst)[m] = ((char *)src)[m];
            if (end)
                break;
        }
        ret = dst;
    } else {
        ret = strcpy_real(dst, src);
        while (src[n]!= '\0') n++;
    }
#ifdef PROTOCOL_OFF
    if (is_in_cxl_nhc)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc)
        thread_ops->log_range_store((char *)dst, ((char *)dst + n));
#endif
    return ret;
}

struct CacheAgentArg {
    unsigned node_id;
    unsigned cpu_id;
};

void *run_cache_agent(void *arg) {
    auto carg = (CacheAgentArg*) arg;
#ifdef CACHE_AGENT_AFFINITY
    set_thread_affinity(carg->cpu_id);
#endif
    unsigned nid = carg->node_id;
    CacheAgent(cache_infos[nid], log_mgrs, nid).run();
    return arg;
}

void rac_init(unsigned nid) {
#ifdef USE_NUMA
    run_on_local_numa();
    cxl_nhc_buf = (char *)remote_numa_alloc(sizeof(CXLPool));
    cxl_hc_buf = (char *)remote_numa_alloc(sizeof(PerNode<LogManager>) + CXL_HC_RANGE);
#else 
    cxl_nhc_buf = new char[CXL_NHC_RANGE];
    cxl_hc_buf = new char[CXL_HC_RANGE];
#endif
    node_local_buf = new char[sizeof(CacheInfo) * NODE_COUNT];

    size_t cxl_hc_off = 0;
    static_assert(CXL_HC_RANGE > sizeof(LogManager[NODE_COUNT]));
    log_mgrs = (LogManager *) cxl_hc_buf;
    for (int i = 0; i < NODE_COUNT; i++)
        new (&log_mgrs[i]) LogManager(i);
    cxl_hc_off += sizeof(LogManager[NODE_COUNT]);

    // reserve space for cxl nhc pool as it needs to be initialized after cxl hc pool
    char *cxl_nhc_pool_buf = cxl_hc_buf + cxl_hc_off;
    cxl_hc_off += sizeof(ExtentPool);
    //align buffer to cache line
    if (uintptr_t remain = (uintptr_t)(cxl_hc_buf + cxl_hc_off) & (CACHE_LINE_SIZE-1))
        cxl_hc_off += CACHE_LINE_SIZE - remain;
    assert(CXL_HC_RANGE > cxl_hc_off);
    cxlhc_pool_initialize(cxl_hc_buf + cxl_hc_off, CXL_HC_RANGE - cxl_hc_off);
    cxlnhc_pool_initialize(cxl_nhc_pool_buf, cxl_nhc_buf, CXL_NHC_RANGE);
    cache_infos = new (node_local_buf) CacheInfo[NODE_COUNT];
    thread_ops = new ThreadOps(log_mgrs, &cache_infos[nid], nid, curr_tid.fetch_add(1, std::memory_order_relaxed));
    init_memory_ops();

#ifndef PROTOCOL_OFF
    unsigned cpu_id = 0;
    for (unsigned i=0; i<NODE_COUNT; i++) {
        int ret;
#if defined(CACHE_AGENT_AFFINITY) && defined(USE_NUMA)
        ret = find_cpu_on_numa(cpu_id, LOCAL_NUMA_ID);
        assert(!ret);
#elif defined(CACHE_AGENT_AFFINITY)
        cpu_id = i;
#endif
        auto arg = new CacheAgentArg{i, cpu_id};
        ret = pthread_create(&cacheAgent_group[i], nullptr, run_cache_agent, arg);
        assert(!ret);
    }
#endif
}

void rac_shutdown() {
#ifndef PROTOCOL_OFF
    complete.store(true);
    for (unsigned i=0; i<NODE_COUNT; i++) {
        void *arg;
        int ret = pthread_join(cacheAgent_group[i], &arg);
        assert(!ret);
        delete (CacheAgentArg*)arg;
    }
#endif
    for (int i = 0; i < NODE_COUNT; i++)
        log_mgrs[i].~LogManager();
    for (int i = 0; i < NODE_COUNT; i++)
        cache_infos[i].~CacheInfo();
    delete thread_ops;
#ifndef USE_NUMA
    delete[] cxl_hc_buf;
    delete[] cxl_nhc_buf;
#endif
    delete[] node_local_buf;
}
