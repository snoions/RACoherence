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
        return (i+1) & ((LOG_BUF_SIZE * 2)-1);
    }

    inline idx_t get_idx(Log *p) {
        assert(p >= &buf[0] && p < &buf[LOG_BUF_SIZE] && "Pointer does not belong to buffer");
        return p - &buf[0];
    }

    inline bool perform_gc(idx_t t) {
        idx_t last = LOG_BUF_SIZE*2;
        for (int i = 0; i < NODE_COUNT; i++) {
            if (i == node_id)
               continue; 
            auto h = heads[i].load(std::memory_order_relaxed);
            if (last == LOG_BUF_SIZE*2)
                last = h;
            else if (h < last && (h > t || last < t))
                last = h;
        }
        //TODO: differentiate between marked and unmarked index types
        idx_t target = last ^ LOG_BUF_SIZE;
        LOG_ERROR("perform gc: target " << target << " t " << t)
        if (t == target)
            return false;
        for (idx_t i = t; i != target ; i = next(i)) {
            buf[i & (LOG_BUF_SIZE-1)].produced.store(false, std::memory_order_relaxed);
        }
        return true;
    }

public:

    Log *get_new_log() {
        auto t = tail.load(std::memory_order_acquire);
        Log *log;
        do {
            log = &buf[t & (LOG_BUF_SIZE-1)];
            if (log->produced.load()) {
                if (gc_mtx.try_lock()) {
                    bool ok = perform_gc(t);
                    gc_mtx.unlock();
                    if (!ok) {
                        LOG_ERROR("get new log blocked, nid " << node_id)
                        return NULL;
                    }
                } else {
                    return NULL;
                }
            }
        } while(!tail.compare_exchange_weak(t, next(t), std::memory_order_release, std::memory_order_relaxed));
        return log;
    }

    void produce_tail(Log *l, bool r) {
        //add tail to pub
        l->is_rel = r;
        l->produced.store(true, std::memory_order_release);
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    //only allows exclusive access 
    Log *take_head(unsigned nid) {
        //return head, check if overlaps with tail
        auto h = heads[nid].load(std::memory_order_relaxed);
        auto t = tail.load(std::memory_order_relaxed);
        if (h == t) {
            LOG_ERROR("take head blocked, h " << h << " curr node " << nid)
            return NULL;
        }
        auto log = &buf[h & (LOG_BUF_SIZE-1)];
        if (!log->produced.load(std::memory_order_acquire)) {
            LOG_ERROR("take head blocked 2, h " << h << " curr node " << nid)
            return NULL;
        }
        return log;
    }

    //only allows exclusive access 
    void consume_head(unsigned nid) {
        //move head
        auto h = heads[nid].load(std::memory_order_relaxed);
        heads[nid].store(next(h), std::memory_order_release);
    }
};

#endif
