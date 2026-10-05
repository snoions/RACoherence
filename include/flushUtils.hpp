#ifndef _CACHE_OPS_H_
#define _CACHE_OPS_H_

#include <stdio.h>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include "config.hpp"

#define CLFLUSH 1
#define CLFLUSHOPT 2
#define CLWB 3

#if !NO_FLUSH
#define WRITEBACK_INST 3
#define INVALIDATE_INST 2 //may not be CLWB
#endif

namespace RACoherence {

constexpr long WRITE_LATENCY_IN_NS = 0;
constexpr long CPU_FREQ_MHZ = 2100;

// Invalidation statistics (STATS builds), per process, i.e. per node:
//   invalidated - cache lines (CACHE_LINE_SIZE each) passed to do_invalidate(), in either
//                 mode: by log processing in eager mode, on access in lazy mode
//                 (CacheLineTracker), and by do_range_invalidate();
//   named       - cache lines named by the logs this node consumed: what eager mode
//                 invalidates and lazy mode marks dirty (CacheInfo::process_log_entry).
// Counted per thread without atomics; each thread adds its counts to the totals when it exits,
// and rac_shutdown() adds the calling thread's before printing. Lines covered by WBINVD_PATH's
// wbinvd() are named but not counted as invalidated.
struct InvalidationStats {
    uint64_t invalidated = 0;
    uint64_t named = 0;

    static std::atomic<uint64_t> &total_invalidated() {
        static std::atomic<uint64_t> total{0};
        return total;
    }
    static std::atomic<uint64_t> &total_named() {
        static std::atomic<uint64_t> total{0};
        return total;
    }
    void add_to_totals() {
        total_invalidated().fetch_add(invalidated, std::memory_order_relaxed);
        total_named().fetch_add(named, std::memory_order_relaxed);
        invalidated = named = 0;
    }
    ~InvalidationStats() { add_to_totals(); }
};

inline InvalidationStats &thread_inv_stats() {
    thread_local InvalidationStats stats;
    return stats;
}

static inline void do_writeback(char *ptr)
{
#if WRITEBACK_INST == CLFLUSH
    __asm__ volatile("clflush %0" : "+m" (*(volatile char *)ptr));
#elif WRITEBACK_INST == CLFLUSHOPT
    __asm__ volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)ptr));
#elif WRITEBACK_INST == CLWB
    __asm__ volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)ptr));
#endif
}

static inline void do_invalidate(char *ptr)
{
    STATS(thread_inv_stats().invalidated++;)
#if INVALIDATE_INST == CLFLUSH
    __asm__ volatile("clflush %0" : "+m" (*(volatile char *)ptr));
#elif INVALIDATE_INST == CLFLUSHOPT
    __asm__ volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)ptr));
#endif
}

static inline void cpu_pause()
{
    __asm__ volatile ("pause" ::: "memory");
}

static inline unsigned long read_tsc(void)
{
    unsigned long var;
    unsigned int hi, lo;

    asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    var = ((unsigned long long int) hi << 32) | lo;

    return var;
}

inline void do_range_writeback(char *data, int len)
{
#if !NO_FLUSH
    char *ptr = (char *)((unsigned long)data & ~CACHE_LINE_MASK);
    for (; ptr < data+len; ptr += CACHE_LINE_SIZE){
        unsigned long etsc = read_tsc() +
            (unsigned long)(WRITE_LATENCY_IN_NS * CPU_FREQ_MHZ/1000);
        do_writeback(ptr);
        while (read_tsc() < etsc) cpu_pause();
    }
#endif
}

inline void do_range_invalidate(char *data, int len)
{
#if !NO_FLUSH
    char *ptr = (char *)((unsigned long)data & ~CACHE_LINE_MASK);
    for (; ptr < data+len; ptr += CACHE_LINE_SIZE){
        unsigned long etsc = read_tsc() +
            (unsigned long)(WRITE_LATENCY_IN_NS * CPU_FREQ_MHZ/1000);
        do_invalidate(ptr);
        while (read_tsc() < etsc) cpu_pause();
    }
#endif
}

static inline void writeback_fence()
{
#if WRITEBACK_INST == CLFLUSHOPT || WRITEBACK_INST == CLWB
    __asm__ volatile("sfence":::"memory");
#endif
}

static inline void invalidate_fence()
{
#if !NO_FLUSH
    __asm__ volatile("mfence":::"memory");
#endif
}

static inline void wbinvd() {
#if !NO_FLUSH
    FILE *fd = fopen(WBINVD_PATH, "r");
    if (fd == nullptr) {
        perror("unable to execute wbinvd");
        std::exit(EXIT_FAILURE);
    }
    fclose(fd);
#endif
}

} // RACoherence

#endif
