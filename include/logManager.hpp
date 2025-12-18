#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <iostream>

#include "config.hpp"
#include "clh_mutex.hpp"
#include "logger.hpp"
#include "spmcQueue.hpp"
#include "utils.hpp"

namespace RACoherence {

constexpr size_t LOG_SIZE = 1ull << 10;
//LOG_BUF_SIZE must be power of 2
constexpr size_t LOG_BUF_SIZE = 1ull << 7;

class LogManager;

//index into LogManager's pub array, monotonically increases
using idx_t = size_t;

inline idx_t next_round(idx_t idx) {
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

    unsigned node_id;

    spmc_bounded_queue<Log *, LOG_BUF_SIZE> freelist;

    idx_t bound = next_round(0);

    alignas(CACHE_LINE_SIZE)
    std::atomic<Log *>pub[LOG_BUF_SIZE];

    Log buf[LOG_BUF_SIZE];

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> tail{0};

    // ensure atomic and mutex arrays are aligned to cache line boundaries
    CacheAligned<std::atomic<idx_t>> heads[NODE_COUNT];

    CacheAligned<std::mutex> head_mtxs[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    CLHMutex gc_mtx;

    // try aligning each bool to different cache line if contention is high
    alignas(CACHE_LINE_SIZE)
    std::atomic<bool> subscribers[NODE_COUNT];

    inline void perform_gc() {
        idx_t new_b = next_round(bound);
        for (unsigned i = 0; i < NODE_COUNT; i=(i+1==node_id)? i+2: i+1) {
            if (!subscribers[i].load(std::memory_order_acquire))
                continue;
            auto h = next_round(heads[i].load(std::memory_order_relaxed));
            if (new_b == next_round(bound))
                new_b = h;
            else if (bound <= h && h < new_b)
                new_b = h;
            else if (h < new_b && new_b < bound)
                new_b = h;
            else if (new_b < bound && bound <= h)
                new_b = h;
        }
        LOG_DEBUG("node " << node_id << " perform gc new bound " << new_b << " bound " << bound)

        assert(bound <= new_b);
        for (idx_t i = bound; i != new_b; i++) {
            //handle spurious failures from enqueue
            while(!freelist.enqueue(pub[get_idx(i)].load(std::memory_order_relaxed)));
        }
        bound = new_b;
    }

public:

    LogManager(unsigned nid): node_id(nid) {
        for (unsigned i = 0; i < NODE_COUNT; i++)
            subscribers[i].store(true);
        for (unsigned i = 0; i < LOG_BUF_SIZE; i++) {
            pub[i].store(&buf[i], std::memory_order_relaxed);
            auto ok = freelist.enqueue(&buf[i]);
            assert(ok);
        }
    }

    inline void set_subscribed(unsigned nid, bool subscribed) {
        subscribers[nid].store(subscribed, std::memory_order_release);
    }

    inline bool is_subscribed(unsigned nid) {
        return subscribers[nid].load(std::memory_order_acquire);
    }

    Log *get_new_log() {
         Log *log;
         auto ok = freelist.dequeue(log);
         if (!ok) {
             gc_mtx.lock();
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

} // RACoherence

#endif
