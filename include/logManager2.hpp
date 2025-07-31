#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <mutex>

#include "MPMCQueue.hpp"
#include "config.hpp"
#include "logger.hpp"

constexpr size_t LOG_SIZE = 1ull << 6;
constexpr size_t LOG_BUF_SIZE = 1ull << 10;

extern thread_local unsigned node_id;

class LogManager;

class alignas(CACHE_LINE_SIZE) Log {
    friend LogManager;

    using Entry = uintptr_t;
    using Data = Entry[LOG_SIZE];
    using iterator = Entry *;
    using const_iterator = const Entry *;

    std::array<uintptr_t, LOG_SIZE> entries;
    size_t size = 0;
    bool is_rel = false;
    std::atomic<bool> produced {false};

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

    const_iterator begin() const {
        return &entries[0];
    }

    const_iterator end() const {
        return &entries[size];
    }
};

class alignas(CACHE_LINE_SIZE) LogManager {
    // node-local allocator to reduce coherence messages between nodes
    using idx_t = size_t;

    //TODO: use
    class MarkedIndex {
        size_t idx = 0;
    public:
        MarkedIndex(size_t i): idx(i) {};
        inline MarkedIndex next() {
            return {(idx+1) & ((LOG_BUF_SIZE << 1)-1)};
        }
        inline MarkedIndex flip() {
            return {idx ^ LOG_BUF_SIZE};
        }
        inline idx_t get_idx(){
            return idx & (LOG_BUF_SIZE -1);
        }
    };

    //TODO: change to idx type
    MPMCRingBuffer<Log *, LOG_BUF_SIZE> freelist;
    Log *pub[LOG_BUF_SIZE];

    alignas(CACHE_LINE_SIZE)
    Log buf[LOG_BUF_SIZE];

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> tail {0};
    //TODO: pad to different cache lines
    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> heads[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> last_head;
    
    alignas(CACHE_LINE_SIZE)
    std::mutex head_mtxs[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::mutex gc_mtx;

    inline idx_t next(idx_t i) {
        //LOG_BUF_SIZE must be power of 2
        return (i+1) & ((LOG_BUF_SIZE << 1)-1);
    }

    //TODO: check memory order
    inline void perform_gc() {
        if (!gc_mtx.try_lock())
            return;
        auto t = tail.load();
        idx_t last = LOG_BUF_SIZE*2;
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == node_id)
               continue;
            auto h = heads[i].load(std::memory_order_relaxed) ^ LOG_BUF_SIZE;
            LOG_ERROR("node " << node_id << " perform gc head " << i << " = " << h)
            if (last == LOG_BUF_SIZE*2)
                last = h;
            else if (t <= h && h < last)
                last = h;
            else if (h < last && last < t)
                last = h;
            else if (last < t && t <= h)
                last = h;
        }
        LOG_ERROR("node " << node_id << " perform gc last " << last << " t " << t)
        //triggered here:
        assert((last >= t && last - t <= LOG_BUF_SIZE) || (last < t  && t - last >= LOG_BUF_SIZE));

        for (idx_t i = t; i != last ; i = next(i)) {
            auto ok = freelist.enqueue(pub[i & (LOG_BUF_SIZE-1)]);
            assert(ok);
        }
        //TODO: remove last_head later
        last_head.store(last);
        gc_mtx.unlock();
    }

public:

    LogManager() {
        for (int i =0; i < LOG_BUF_SIZE; i++) {
            auto ok = freelist.enqueue(&buf[i]);
            assert(ok);
        }
    }

    Log *get_new_log() {
         Log *log;
         auto ok = freelist.dequeue(log);
         if (!ok) {
             perform_gc();
             auto ok = freelist.dequeue(log);
             if (!ok)
                return NULL;
         }
         log->size = 0;
         return log;
    }

    void produce_tail(Log *l, bool r) {
        auto t = tail.load();
        while(!tail.compare_exchange_weak(t, next(t)));
        assert(t != last_head);
        l->is_rel = r;
        pub[t] = l;
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    //only allows exclusive access 
    Log *take_head(unsigned bnid, unsigned nid) {
        //return head, check if overlaps with tail
        auto h = heads[nid].load(std::memory_order_relaxed);
        auto t = tail.load(std::memory_order_relaxed);
        if (h == t) {
            return NULL;
        }
        auto log = pub[h & (LOG_BUF_SIZE-1)];
        return log;
    }

    //only allows exclusive access 
    void consume_head(unsigned nid) {
        //move head
        auto h = heads[nid].load(std::memory_order_relaxed);
        heads[nid].store(next(h), std::memory_order_relaxed);
    }
};

#endif
