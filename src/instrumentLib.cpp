#include <dlfcn.h>
#include <unistd.h>
#include "instrumentLib.hpp"
#include "runtime.hpp"

namespace RACoherence {

void * (*volatile memcpy_real)(void * dst, const void *src, size_t n) = nullptr;
void * (*volatile memmove_real)(void * dst, const void *src, size_t len) = nullptr;
void (*volatile bzero_real)(void * dst, size_t len) = nullptr;
void * (*volatile memset_real)(void * dst, int c, size_t len) = nullptr;
char * (*volatile strcpy_real)(char * dst, const char *src) = nullptr;
ssize_t (*volatile read_real)(int fd, void* buf, size_t count) = nullptr;

void instrument_lib() {
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
    if (!read_real) {
        read_real = (ssize_t (*)(int fd, void* buf, size_t count)) 1;
        read_real = (ssize_t (*)(int fd, void* buf, size_t count))dlsym(RTLD_NEXT, "read");
    }
}

} // RACoherence

extern "C" {

void * memcpy(void * dst, const void * src, size_t n) {
    void *ret;
    bool is_in_cxl_nhc_src = in_cxl_nhc_mem((char *)src);
    bool is_in_cxl_nhc_dst = in_cxl_nhc_mem((char *)dst);
    char *dst_begin = (char *)dst;
    char *dst_end = dst_begin + n;
#if PROTOCOL_OFF
    if (is_in_cxl_nhc_src)
        do_range_invalidate((char *)src, n);
    if (is_in_cxl_nhc_dst)
        invalidate_boundaries(dst_begin, dst_end); 
#elif !defined(EAGER_INVALIDATE)
    char *src_begin = (char *)src;
    char *src_end = src_begin + n;
    if (is_in_cxl_nhc_src)
        check_range_invalidate(src_begin, src_end);
    if (is_in_cxl_nhc_dst)
        invalidate_boundaries(dst_begin, dst_end); 
#endif
    if (((uintptr_t)memcpy_real) < 2) {
        for(unsigned i=0;i<n;i++) {
            ((volatile char *)dst)[i] = ((char *)src)[i];
        }
        ret = dst;
    } else
        ret = memcpy_real(dst, src, n);
#if PROTOCOL_OFF
    if (is_in_cxl_nhc_dst)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc_dst)
        thread_ops->log_range_store(dst_begin, dst_end);
#endif
    return ret;
}

void * memmove(void *dst, const void *src, size_t n) {
    void *ret;
    bool is_in_cxl_nhc_src = in_cxl_nhc_mem((char *)src);
    bool is_in_cxl_nhc_dst = in_cxl_nhc_mem((char *)dst);
    char *dst_begin = (char *)dst;
    char *dst_end = dst_begin + n;
#if PROTOCOL_OFF
    if (is_in_cxl_nhc_src)
        do_range_invalidate((char *)src, n);
    if (is_in_cxl_nhc_dst)
        invalidate_boundaries(dst_begin, dst_end); 
#elif !defined(EAGER_INVALIDATE)
    char *src_begin = (char *)src;
    char *src_end = src_begin + n;
    if (is_in_cxl_nhc_src)
        check_range_invalidate(src_begin, src_end);
    if (is_in_cxl_nhc_dst)
        invalidate_boundaries(dst_begin, dst_end); 
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
#if PROTOCOL_OFF
    if (is_in_cxl_nhc_dst)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc_dst)
        thread_ops->log_range_store(dst_begin, dst_end);
#endif
    return ret;
}

void * memset(void *dst, int c, size_t n) {
    void *ret;
    bool is_in_cxl_nhc = in_cxl_nhc_mem((char *)dst);
    char *dst_begin = (char *)dst;
    char *dst_end = dst_begin + n;
#if PROTOCOL_OFF
    if (is_in_cxl_nhc)
        invalidate_boundaries(dst_begin, dst_end);
#elif !defined(EAGER_INVALIDATE)
    if(is_in_cxl_nhc)
        invalidate_boundaries(dst_begin, dst_end);
#endif
    if (((uintptr_t)memset_real) < 2) {
        for(unsigned i=0;i<n;i++) {
            ((volatile char *)dst)[i] = (char) c;
        }
        ret = dst;
    } else
        ret = memset_real(dst, c, n);
#if PROTOCOL_OFF
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
#if PROTOCOL_OFF
    if (is_in_cxl_nhc)
        invalidate_boundaries(dst_begin, dst_end);
#elif !defined(EAGER_INVALIDATE)
    if(is_in_cxl_nhc)
        invalidate_boundaries(dst_begin, dst_end);
#endif
    if (((uintptr_t)bzero_real) < 2) {
        for(size_t s=0;s<n;s++) {
            ((volatile char *)dst)[s] = 0;
        }
    } else
        bzero_real(dst, n);
#if PROTOCOL_OFF
    if (is_in_cxl_nhc)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc)
        thread_ops->log_range_store(dst_begin, dst_end);
#endif
}

char * strcpy(char *dst, const char *src) {
    char *ret;
    bool is_in_cxl_nhc_src = in_cxl_nhc_mem((char *)src);
    bool is_in_cxl_nhc_dst = in_cxl_nhc_mem((char *)dst);
    size_t n = 0;
    // we cannot invalidate ahead-of-time because the length is unknown
#if PROTOCOL_OFF || !defined(EAGER_INVALIDATE)
    bool need_invalidate = true;
#else
    bool need_invalidate = false;
#endif
    if (((uintptr_t)strcpy_real) < 2 || need_invalidate) {
        while (true) {
#if PROTOCOL_OFF
            if (is_in_cxl_nhc_src)
                do_invalidate((char *)&src[n]);
#elif !defined(EAGER_INVALIDATE)
            if (is_in_cxl_nhc_src)
                check_invalidate((char *)&src[n]);
#endif
            bool end = false;
            for(;((uintptr_t)&src[n] & CACHE_LINE_MASK); n++) {
                if (src[n] == '\0') {
                    n++;
                    end = true;
                    break;
                }
            }
            if (end)
                break;
        }
#if PROTOCOL_OFF
        if (is_in_cxl_nhc_dst)
            invalidate_boundaries(dst, (char *)&dst[n]);
#elif !defined(EAGER_INVALIDATE)
        if (is_in_cxl_nhc_dst)
            invalidate_boundaries(dst, (char *)&dst[n]);
#endif
        for (int i; i < n; i++)
            ((volatile char *)dst)[i] = ((char *)src)[i];
        ret = dst;
    } else {
        ret = strcpy_real(dst, src);
        while (src[n]!= '\0') n++;
    }
#if PROTOCOL_OFF
    if (is_in_cxl_nhc_dst)
        do_range_flush((char *)dst, n);
#else
    if (is_in_cxl_nhc_dst)
        thread_ops->log_range_store((char *)dst, ((char *)dst + n));
#endif
    return ret;
}

ssize_t read(int fd, void* buf, size_t count) {
    ssize_t ret;
    bool is_in_cxl_nhc = in_cxl_nhc_mem((char *)buf);
    char *buf_begin = (char *)buf;
    char *buf_end = buf_begin + count;
#if PROTOCOL_OFF
    if (is_in_cxl_nhc)
        invalidate_boundaries(buf_begin, buf_end);
#elif !defined(EAGER_INVALIDATE)
    if(is_in_cxl_nhc)
        invalidate_boundaries(buf_begin, buf_end);
#endif
    if (((uintptr_t)read_real) < 2) {
        LOG_ERROR("unable to find read() with dlsym") 
        exit(EXIT_FAILURE); 
    } else
        ret = read_real(fd, buf, count);
#if PROTOCOL_OFF
    if (is_in_cxl_nhc)
        do_range_flush((char *)buf, count);
#else
    if (is_in_cxl_nhc)
        thread_ops->log_range_store(buf_begin, buf_end);
#endif
    return ret;
}

} // extern "C"
