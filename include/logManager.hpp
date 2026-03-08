#ifndef _LOG_MANAGER_H_
#define _LOG_MANAGER_H_

#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <thread>

#include "config.hpp"
#include "clh_mutex.hpp"
#include "logger.hpp"
#include "utils.hpp"

namespace RACoherence {

class LogManager;

//index into LogManager's pub array, monotonically increases
using idx_t = size_t;

inline idx_t next_round(idx_t idx) {
    return idx + LOG_COUNT;
}

inline idx_t prev_round(idx_t idx) {
    return idx - LOG_COUNT;
}

inline size_t get_idx(idx_t idx){
    return idx & (LOG_COUNT -1);
}

struct alignas(CACHE_LINE_SIZE) Log {
    friend LogManager;

    using Entry = uintptr_t;
    using Data = Entry[LOG_SIZE];
    using iterator = Entry *;
    using const_iterator = const Entry *;

    Data entries;
    size_t size = 0;

public:
    inline size_t get_size() {
        return size;
    }

    inline bool is_full() {
        return size == LOG_SIZE;
    }

    inline void write(uintptr_t cl_addr) {
        assert(size < LOG_SIZE);
        entries[size++] = cl_addr;
    }

    const_iterator begin() const {
        return &entries[0];
    }

    const_iterator end() const {
        return &entries[size];
    }
};

struct alignas(CACHE_LINE_SIZE) PubEntry {
    std::atomic<Log *> log;
    std::atomic<idx_t> idx{0};
    bool is_rel = false;
};

class alignas(CACHE_LINE_SIZE) LogManager {
    Log buf[LOG_COUNT];

    PubEntry pub[LOG_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> tail{0};

    // ensure atomic and mutex arrays are aligned to cache line boundaries
    CacheAligned<std::atomic<idx_t>> heads[NODE_COUNT];

    CacheAligned<CLHMutex> head_mtxs[NODE_COUNT];

    // try aligning each bool to different cache line if contention is high
    alignas(CACHE_LINE_SIZE)
    std::atomic<bool> subscribers[NODE_COUNT];
    
    unsigned node_id;

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> alloc_tail{next_round(0)};

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> alloc_head{0};    

    inline idx_t perform_gc() {
        idx_t at = alloc_tail.load(std::memory_order_relaxed);
        idx_t new_at = next_round(at);
        bool subscribed = false;
        for (unsigned i = 0; i < NODE_COUNT; i++) {
            if (i == node_id)
                continue;
            if (!subscribers[i].load(std::memory_order_acquire))
                continue;
            subscribed = true;
            auto h = next_round(heads[i].load(std::memory_order_acquire));
            if(h < new_at)
                new_at = h;
        }

        assert(at <= new_at);
        if (!subscribed) {
            // hack to avoid race condition with producers
            // improve later
            for (idx_t i = alloc_tail; i != new_at; i++) {
                const auto &entry = pub[get_idx(i)];
                Log *log = entry.log.load(std::memory_order_relaxed);
                if (entry.idx.load(std::memory_order_acquire) != prev_round(i) + 1) {
                    new_at = i; 
                    break;
                }
            }

        }
        LOG_DEBUG("node " << node_id << " perform gc new alloc_tail " << new_at << " alloc_tail " << alloc_tail)
        at = alloc_tail.load(std::memory_order_relaxed);
        if (new_at > at)
            if(alloc_tail.compare_exchange_strong(at, new_at, std::memory_order_acq_rel, std::memory_order_relaxed))
                return new_at;
        return at;
    }

public:

    LogManager(unsigned nid): node_id(nid) {
        for (unsigned i = 0; i < NODE_COUNT; i++)
            subscribers[i].store(true);
        for (unsigned i = 0; i < LOG_COUNT; i++) {
            pub[i].log.store(&buf[i], std::memory_order_relaxed);
        }
    }

    inline void set_subscribed(unsigned nid, bool subscribed) {
        subscribers[nid].store(subscribed, std::memory_order_release);
    }

    inline bool is_subscribed(unsigned nid) {
        return subscribers[nid].load(std::memory_order_acquire);
    }

    Log *get_new_log() {
         idx_t at = alloc_tail.load(std::memory_order_acquire);
         idx_t ah = alloc_head.load(std::memory_order_relaxed);
         while (ah < at) {
            if (alloc_head.compare_exchange_weak(ah, ah+1, std::memory_order_relaxed)) {
                Log *log = pub[(get_idx(ah))].log.load(std::memory_order_relaxed);
                log->size = 0;
                return log;
            }
         }
         if (ah >= alloc_tail.load(std::memory_order_acquire)) {
            at = perform_gc();
         }
         while (ah < at) {
            if (alloc_head.compare_exchange_weak(ah, ah+1, std::memory_order_relaxed)) {
                Log *log = pub[(get_idx(ah))].log.load(std::memory_order_relaxed);
                log->size = 0;
                return log;
            }
         }
         return nullptr;
    }

    //returns current release clock
    clock_t produce_tail(Log *l, bool r) {
        auto t = tail.fetch_add(1, std::memory_order_relaxed);
        auto &entry = pub[get_idx(t)];
        entry.is_rel = r;
        entry.log.store(l, std::memory_order_relaxed);
        entry.idx.store(t+1, std::memory_order_release);
        return t+1;
    }

    CLHMutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    //only allows exclusive access on each node 
    const PubEntry *take_head(unsigned nid) {
        //return head, check if overlaps with tail
        auto h = heads[nid].load(std::memory_order_relaxed);
        const auto &entry = pub[get_idx(h)];
        if (entry.idx.load(std::memory_order_acquire) != h+1) {
            return NULL;
        }
        return &entry;
    }

    //only allows exclusive access on each node
    void consume_head(unsigned nid) {
        //move head
        auto h = heads[nid].load(std::memory_order_relaxed);
        heads[nid].store(h+1, std::memory_order_release);
    }
};

} // RACoherence

#endif
