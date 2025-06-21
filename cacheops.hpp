#ifndef _CACHE_OPS_H_
#define _CACHE_OPS_H_
#include <iostream>

#include "config.hpp"

#define CLFLUSH 1
#define CLFLUSHOPT 2
#define CLWB 3

#define FLUSH_INST 2
#define INVALIDATE_INST 2 //may not be CLWB

static inline void do_flush(char *addr)
{
    volatile char *ptr = (char *)((virt_addr_t)addr & CACHE_LINE_MASK);
#if FLUSH_INST == CLFLUSH
    __asm__ volatile("clflush %0" : "+m" (*ptr));
#elif FLUSH_INST == CLFLUSHOPT
    std::cout << "here " << addr << std::endl;
    __asm__ volatile(".byte 0x66; clflush %0" : "+m" (*ptr));
#elif FLUSH_INST == CLWB
    __asm__ volatile(".byte 0x66; xsaveopt %0" : "+m" (*ptr));
#endif
}

static inline void do_invalidate(char *addr)
{
    volatile char *ptr = (char *)((virt_addr_t)addr & CACHE_LINE_MASK);
#if INVALIDATE_INST == CLFLUSH
    __asm__ volatile("clflush %0" : "+m" (*ptr));
#elif INVALIDATE_INST == CLFLUSHOPT
    __asm__ volatile(".byte 0x66; clflush %0" : "+m" (*ptr));
#endif
}

static inline void flush_fence()
{
#if FLUSH_INST == CLFLUSHOPT || FLUSH_ISNT == CLWB
    __asm__ volatile("sfence");
#endif
}

static inline void invalidate_fence()
{
    __asm__ volatile("mfence");
}
#endif
