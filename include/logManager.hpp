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

//index into LogManager's pub array that does not wrap around
using idx_t = size_t;

inline idx_t flip(idx_t idx) {
    return idx + LOG_BUF_SIZE;
}
inline size_t get_idx(idx_t idx){
    return idx & (LOG_BUF_SIZE -1);
}

class alignas(CACHE_LINE_SIZE) Log {
    friend LogManager;

    using Entry = uintptr_t;
    using Data = Entry[LOG_SIZE];
    using iterator = Entry *;
    using const_iterator = const Entry *;

    Data entries;
    std::atomic<idx_t> idx{0};
    size_t size = 0;
    bool is_rel = false;

public:

    inline bool write(uintptr_t cl_addr) {
        if (size == LOG_SIZE)
            return false;
        entries[size++] = cl_addr;
        return true;
    }

    bool is_release() {
        return is_rel;
    }

    clock_t get_log_idx() {
        return idx.load(std::memory_order_relaxed);
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

    spmc_bounded_queue<Log *, LOG_BUF_SIZE> freelist;
    idx_t bound = flip(0);

    clock_t rel_clk = 0;

    alignas(CACHE_LINE_SIZE)
    std::atomic<Log *>pub[LOG_BUF_SIZE];

    alignas(CACHE_LINE_SIZE)
    Log buf[LOG_BUF_SIZE];

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> tail{0};

    //TODO: pad to different cache lines
    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> heads[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::mutex head_mtxs[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::mutex gc_mtx;

    //TODO: check memory order
    inline void perform_gc() {
        idx_t new_b = flip(bound);
        for (int i = 0; i < NODE_COUNT; i=(i+1==node_id)? i+2: i+1) {
            auto h = flip(heads[i].load(std::memory_order_relaxed));
            if (new_b == flip(bound))
                new_b = h;
            else if (bound <= h && h < new_b)
                new_b = h;
            else if (h < new_b && new_b < bound)
                new_b = h;
            else if (new_b < bound && bound <= h)
                new_b = h;
        }
        LOG_INFO("node " << node_id << " perform gc new bound " << new_b << " bound " << bound)

        assert(bound <= new_b);
        for (idx_t i = bound; i != new_b; i++) {
            //handle spurious failures
            while(!freelist.enqueue(pub[get_idx(i)].load(std::memory_order_relaxed)));
        }
        bound = new_b;
    }

public:

    LogManager() {
        for (int i =0; i < LOG_BUF_SIZE; i++) {
            pub[i].store(&buf[i], std::memory_order_relaxed);
            auto ok = freelist.enqueue(&buf[i]);
            assert(ok);
        }
    }

    Log *get_new_log() {
         Log *log;
         auto ok = freelist.dequeue(log);
         if (!ok) {
             if (gc_mtx.try_lock()) {
                 //check again after locking
                ok = freelist.dequeue(log);
                if (ok) {
                    gc_mtx.unlock();
                    return new(log) Log();
                }
                perform_gc();
                gc_mtx.unlock();
                ok = freelist.dequeue(log);
                if(!ok)
                    return NULL;
            } else
                return NULL;
         }
         return new(log) Log();
    }

    //returns current release clock
    clock_t produce_tail(Log *l, bool r) {
        auto t = tail.fetch_add(1, std::memory_order_relaxed);
        l->is_rel = r;
        pub[get_idx(t)].store(l, std::memory_order_relaxed);
        l->idx.store(t+1, std::memory_order_release);
        return t+1;
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    //only allows exclusive access 
    Log *take_head(unsigned nid) {
        //return head, check if overlaps with tail
        auto h = heads[nid].load(std::memory_order_relaxed);
        auto l = pub[get_idx(h)].load(std::memory_order_relaxed);
        if (l->idx.load(std::memory_order_acquire) != h+1) {
            return NULL;
        }
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
