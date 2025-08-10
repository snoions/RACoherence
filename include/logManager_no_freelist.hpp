#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <mutex>

#include "SPMCQueue.hpp"
#include "config.hpp"
#include "logger.hpp"

constexpr size_t LOG_SIZE = 1ull << 6;
//LOG_BUF_SIZE must be power of 2
constexpr size_t LOG_BUF_SIZE = 1ull << 10;

extern thread_local unsigned node_id;

class LogManager;

constexpr size_t PAR_BIT = LOG_BUF_SIZE << 1;

//index into LogManager's pub array with high bit as parity
using par_idx_t = size_t;

inline size_t get_idx(par_idx_t idx){
    return idx & (LOG_BUF_SIZE -1);
}

class alignas(CACHE_LINE_SIZE) Log {
    friend LogManager;

    using Entry = uintptr_t;
    using Data = Entry[LOG_SIZE];
    using iterator = Entry *;
    using const_iterator = const Entry *;

    Data entries;
#ifdef LOG_USE_PAR_INDEX
    std::atomic<par_idx_t> pidx{0};
#endif
    size_t size = 0;
    clock_t rel_clk = 0;

public:

    inline bool write(uintptr_t cl_addr) {
        if (size == LOG_SIZE)
            return false;
        entries[size++] = cl_addr;
        return true;
    }

    bool is_release() {
        return rel_clk != 0;
    }

    clock_t get_rel_clk() {
        return rel_clk;
    }

    const_iterator begin() const {
        return &entries[0];
    }

    const_iterator end() const {
        return &entries[size];
    }
};

class alignas(CACHE_LINE_SIZE) LogManager {
    // logs are allocated in buffer private to each node to reduce coherence messages between nodes

    struct alignas(CACHE_LINE_SIZE) aligned_mutex {
        std::mutex data;
    };

    alignas(CACHE_LINE_SIZE)
    std::atomic<par_idx_t> bound{LOG_BUF_SIZE};

    clock_t rel_clk = 0;

#ifdef LOG_USE_PAR_INDEX
    alignas(CACHE_LINE_SIZE)
    std::atomic<Log *>pub[LOG_BUF_SIZE];
#else
    Log *pub[LOG_BUF_SIZE];
#endif

    alignas(CACHE_LINE_SIZE)
    Log buf[LOG_BUF_SIZE];

    alignas(CACHE_LINE_SIZE)
    std::mutex tail_mtx;

    alignas(CACHE_LINE_SIZE)
    std::atomic<par_idx_t> alloc_tail{0};

#ifdef LOG_USE_PAR_INDEX
    par_idx_t tail = 0;
#else
    alignas(CACHE_LINE_SIZE)
    std::atomic<par_idx_t> tail{0};
#endif

    //TODO: pad to different cache lines
    alignas(CACHE_LINE_SIZE)
    std::atomic<par_idx_t> heads[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::mutex head_mtxs[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::mutex gc_mtx;

    //TODO: check memory order
    inline void perform_gc() {
        auto b = bound.load(std::memory_order_relaxed);
        par_idx_t new_b = b + LOG_BUF_SIZE + 1;
        for (int i = 0; i < NODE_COUNT; i=(i+1==node_id)? i+2: i+1) {
            auto h = heads[i].load(std::memory_order_relaxed) + LOG_BUF_SIZE;
            if (h < new_b)
                new_b = h;
        }
        LOG_INFO("node " << node_id << " perform gc new bound " << new_b << " b " << b)

        assert(new_b >= b && new_b < b + LOG_BUF_SIZE + 1);
        bound.store(new_b, std::memory_order_relaxed);
    }

public:

    LogManager() {
        for (int i =0; i < LOG_BUF_SIZE; i++) {
#ifdef LOG_USE_PAR_INDEX
            pub[i].store(&buf[i], std::memory_order_relaxed);
#else
            pub[i] = &buf[i];
#endif
        }
    }

    Log *get_new_log() {
        Log *log;
        auto at = alloc_tail.load(std::memory_order_relaxed);
        do {
            if (at == bound.load(std::memory_order_relaxed)) {
                if (gc_mtx.try_lock()) {
                    //check again after locking
                    if (at == bound.load(std::memory_order_relaxed)) {
                        perform_gc();
                        gc_mtx.unlock();
                        if (at == bound.load(std::memory_order_relaxed))
                            return NULL;
                    } else
                        gc_mtx.unlock();
                } else
                    return NULL;
            }
#ifdef LOG_USE_PAR_INDEX
            log = pub[get_idx(at)].load(std::memory_order_acquire);
#else
            log = pub[get_idx(at)];
#endif
        } while (!alloc_tail.compare_exchange_weak(at, at+1, std::memory_order_relaxed));
        return new(log) Log();
    }

    //returns current release clock
    size_t produce_tail(Log *l, bool r) {
        size_t ret;
        tail_mtx.lock();
        if (r) {
            rel_clk++;
            l->rel_clk = rel_clk;
        }
        ret = rel_clk;
#ifdef LOG_USE_PAR_INDEX
        // pidx-based version can be lock-free if tail is CASed and rel_clk of last release log is found by backward search, but the search could be expensive
        tail++;
        l->pidx.store(tail, std::memory_order_relaxed);
        pub[get_idx(tail-1)].store(l, std::memory_order_release);
#else
        auto t = tail.load(std::memory_order_relaxed);
        pub[get_idx(t)] = l;
        tail.store(t+1, std::memory_order_release);
#endif
        tail_mtx.unlock();
        return ret;
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    //only allows exclusive access 
    Log *take_head(unsigned nid) {
        //return head, check if overlaps with tail
        auto h = heads[nid].load(std::memory_order_relaxed);
#ifdef LOG_USE_PAR_INDEX
        auto l = pub[get_idx(h)].load(std::memory_order_acquire);
        if (!l || l->pidx.load(std::memory_order_relaxed) != h+1) {
            return NULL;
        }
#else
        auto t = tail.load(std::memory_order_acquire);
        if (h == t)
            return NULL;
        auto l = pub[get_idx(h)];
#endif
        return l;
    }

    //only allows exclusive access 
    void consume_head(unsigned nid) {
        //move head
        auto h = heads[nid].load(std::memory_order_relaxed);
        heads[nid].store(h+1, std::memory_order_relaxed);
    }
};

#endif
