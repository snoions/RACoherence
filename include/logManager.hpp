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

    alignas(CACHE_LINE_SIZE)
    Log buf[LOG_BUF_SIZE];

    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> tail {0};
    //TODO: pad to different cache lines
    alignas(CACHE_LINE_SIZE)
    std::atomic<idx_t> heads[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::mutex head_mtxs[NODE_COUNT];

    alignas(CACHE_LINE_SIZE)
    std::mutex gc_mtx;

    inline idx_t next(idx_t i) {
        //LOG_BUF_SIZE must be power of 2
        return (i+1) & ((LOG_BUF_SIZE << 1)-1);
    }

    inline idx_t get_idx(Log *p) {
        assert(p >= &buf[0] && p < &buf[LOG_BUF_SIZE] && "Pointer does not belong to buffer");
        return p - &buf[0];
    }

    //TODO: check memory order
    inline void perform_gc(idx_t t) {
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
        for (idx_t i = t; i != last ; i = next(i)) {
            buf[i & (LOG_BUF_SIZE-1)].produced.store(false, std::memory_order_release);
        }
    }

public:

    //Log *get_new_log() {
    //    std::unique_lock<std::mutex> lk(gc_mtx);
    //    auto t = tail.load();
    //    auto log = &buf[t & (LOG_BUF_SIZE-1)];
    //    if (log->produced.load(std::memory_order_acquire)) {
    //        perform_gc(t);
    //        if (log->produced.load(std::memory_order_acquire))
    //            return NULL;
    //        //idx_t last = LOG_BUF_SIZE*2;
    //        //bool test = false;
    //        //idx_t copy[NODE_COUNT];
    //        //for (int i = 0; i < NODE_COUNT; i++) {
    //        //    if (i == node_id)
    //        //       continue;
    //        //    auto h = heads[i].load(std::memory_order_relaxed) ^ LOG_BUF_SIZE;
    //        //    copy[i] = h;
    //        //    LOG_ERROR("node " << node_id << " perform gc head " << i << " = " << h)
    //        //    if (last == LOG_BUF_SIZE*2)
    //        //        last = h;
    //        //    else if (t <= h && h < last)
    //        //        last = h;
    //        //    else if (h < last && last < t)
    //        //        last = h;
    //        //    else if (last < t && t <= h)
    //        //        last = h;
    //        //    if (h == t)
    //        //        test = true;
    //        //}
    //        //LOG_ERROR("node " << node_id << " perform gc last " << last << " t " << t)
    //        //assert((last== t) == test);
    //        //if (last == t)
    //        //    return NULL;
    //        //for (int i = 0; i < NODE_COUNT; i++) {
    //        //    if (i == node_id)
    //        //        continue;
    //        //    auto h = heads[i].load(std::memory_order_relaxed);
    //        //    if ((h ^ LOG_BUF_SIZE) == t)
    //        //        return NULL;
    //        //}
    //    }
    //    log->produced.store(false, std::memory_order_release);
    //    tail = next(t);
    //    return log;
    //}

    Log *get_new_log() {
        auto t = tail.load(std::memory_order_relaxed);
        Log *log;
        do {
            log = &buf[t & (LOG_BUF_SIZE-1)];
            if (log->produced.load(std::memory_order_acquire)) {
                if (gc_mtx.try_lock()) {
                    perform_gc(t);
                    gc_mtx.unlock();
                    if (log->produced.load(std::memory_order_acquire))
                        return NULL;
                } else {
                    return NULL;
                }
            }
        } while(!tail.compare_exchange_weak(t, next(t), std::memory_order_relaxed, std::memory_order_relaxed));
        return log;
    }

    void produce_tail(Log *l, bool r) {
        l->is_rel = r;
        l->produced.store(true, std::memory_order_release);
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    //only allows exclusive access 
    Log *take_head(unsigned bnid, unsigned nid, bool must_succeed=false) {
        //return head, check if overlaps with tail
        auto h = heads[nid].load(std::memory_order_relaxed);
        auto t = tail.load(std::memory_order_relaxed);
        if (h == t) {
            assert(!must_succeed);
            LOG_INFO("node " << nid << " take head from " << bnid << " blocked, h=t " << h)
            return NULL;
        }
        auto log = &buf[h & (LOG_BUF_SIZE-1)];
        if (!log->produced.load(std::memory_order_acquire)) {
            assert(!must_succeed);
            LOG_INFO("node " << nid << " take head from " << bnid << " blocked 2, h=t " << h)
            return NULL;
        }
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
