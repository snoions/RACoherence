#ifndef _CACHE_OPS_H_
#define _CACHE_OPS_H_
#include <emmintrin.h>

#include "config.hpp"

#define CLFLUSH 1
#define CLFLUSHOPT 2
#define CLWB 3

#define FLUSH_INST 3
#define INVALIDATE_INST 2 //may not be CLWB

constexpr long WRITE_LATENCY_IN_NS = 0;
constexpr long CPU_FREQ_MHZ = 2100;

static inline void do_flush(char *addr)
{
    volatile char *ptr = (char *)((uintptr_t)addr & CACHE_LINE_MASK);
#if FLUSH_INST == CLFLUSH
    __asm__ volatile("clflush %0" : "+m" (*(volatile char *)ptr));
#elif FLUSH_INST == CLFLUSHOPT
    __asm__ volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)ptr));
#elif FLUSH_INST == CLWB
    __asm__ volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)ptr));
#endif
}

static inline void do_invalidate(char *addr)
{
    volatile char *ptr = (char *)((uintptr_t)addr & CACHE_LINE_MASK);
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

inline void do_range_flush(char *data, int len)
{
    char *ptr = (char *)((unsigned long)data & CACHE_LINE_MASK);
    for (; ptr < data+len; ptr += CACHE_LINE_SIZE){
        unsigned long etsc = read_tsc() +
            (unsigned long)(WRITE_LATENCY_IN_NS * CPU_FREQ_MHZ/1000);
        do_flush(ptr);
        while (read_tsc() < etsc) cpu_pause();
    }
}

inline void do_range_invalidate(char *data, int len)
{
    char *ptr = (char *)((unsigned long)data & CACHE_LINE_MASK);
    for (; ptr < data+len; ptr += CACHE_LINE_SIZE){
        unsigned long etsc = read_tsc() +
            (unsigned long)(WRITE_LATENCY_IN_NS * CPU_FREQ_MHZ/1000);
        do_invalidate(ptr);
        while (read_tsc() < etsc) cpu_pause();
    }
}

static inline void flush_fence()
{
#if FLUSH_INST == CLFLUSHOPT || FLUSH_ISNT == CLWB
    __asm__ volatile("sfence":::"memory");
#endif
}

static inline void invalidate_fence()
{
#if FLUSH_INST == CLFLUSHOPT || FLUSH_ISNT == CLFLUSH
    __asm__ volatile("mfence":::"memory");
#endif
}
#endif
