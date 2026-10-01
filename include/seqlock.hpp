// seqlock.hpp - a bare sequence lock. No cache flushes or invalidations.
//
// Writers are mutually exclusive: an odd sequence number means a write is in progress,
// and a writer takes the lock by CASing an even sequence to odd. Readers never write
// shared state: they read the sequence, read the data, read the sequence again, and retry
// if it was odd or changed.
//
// Memory ordering follows Boehm, "Can Seqlocks Get Along With Programming Language Memory
// Models?" (MSPC 2012): data protected by the seqlock is accessed with relaxed atomics
// (load_relaxed / store_relaxed below), the writer issues a release fence after making
// the sequence odd, and the reader issues an acquire fence before re-reading it. Plain
// non-atomic data accesses would be a data race in the C++ model even though the retry
// discards the result.
//
// Requires GCC or Clang (uses __atomic builtins for the data accesses, so the protected
// data keeps its existing type, e.g. GlobalEntry).
 
#pragma once
 
#include <atomic>
#include <cstdint>
 
#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
static inline void seqlock_cpu_relax() { _mm_pause(); }
#else
static inline void seqlock_cpu_relax() {}
#endif
 
// One lock per cache line, so that neighbouring locks in an array do not share a line.
struct alignas(64) SeqLock {
    std::atomic<uint32_t> seq{0};
 
    // ---- writer ----
 
    void write_lock() {
        uint32_t s = seq.load(std::memory_order_relaxed);
        for (;;) {
            if ((s & 1u) == 0 &&
                seq.compare_exchange_weak(s, s + 1, std::memory_order_acquire, std::memory_order_relaxed))
                break;
            seqlock_cpu_relax();
            s = seq.load(std::memory_order_relaxed);
        }
        // Order the odd sequence before the data stores that follow.
        std::atomic_thread_fence(std::memory_order_release);
    }
 
    void write_unlock() {
        // Only the lock holder writes seq while it is odd, so a plain increment is safe.
        seq.store(seq.load(std::memory_order_relaxed) + 1, std::memory_order_release);
    }
 
    // ---- reader ----
 
    // Returns an even sequence number to pass to read_retry().
    uint32_t read_begin() const {
        uint32_t s;
        while ((s = seq.load(std::memory_order_acquire)) & 1u) seqlock_cpu_relax();
        return s;
    }
 
    // True if a write overlapped the read and it must be repeated.
    bool read_retry(uint32_t start) const {
        // Order the data loads before the second sequence load.
        std::atomic_thread_fence(std::memory_order_acquire);
        return seq.load(std::memory_order_relaxed) != start;
    }
};

