#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <mutex>

#include "blockAllocator.hpp"
#include "config.hpp"

constexpr size_t LOG_SIZE = 1ull << 6;
constexpr size_t LOG_BUF_SIZE = 1ull << 10;

class LogBuffer;

class alignas(CACHE_LINE_SIZE) Log {
    friend LogBuffer;

    using Entry = virt_addr_t;
    using Data = std::array<Entry, LOG_SIZE>;
    using iterator = Data::iterator;
    using const_iterator = Data::const_iterator;

    std::atomic<unsigned> ref_cn {0};
    std::array<uintptr_t, LOG_SIZE> entries = {};
    size_t size = 0;
    std::atomic<Log *> next {nullptr};
    bool is_rel = false;

    void produce(bool r) {
        is_rel = r;
        ref_cn = 2*NODE_COUNT-1; //tail + (NODE_COUNT-1) heads + (NODE_COUNT-1) user refs
    }

    // returns whether the log may be deleted
    unsigned consume() {
        assert(is_produced());
        auto prev = ref_cn.fetch_sub(1, std::memory_order_relaxed);
        return prev-1 == 0;
    }

public:
    inline unsigned is_produced() {
        return ref_cn > 0;
    }

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
        return entries.begin();
    }

    const_iterator end() const {
        return entries.end();
    }
};

class alignas(CACHE_LINE_SIZE) LogBuffer {
    // node-local allocator to reduce coherence messages between nodes
    BlockAllocator<Log, LOG_BUF_SIZE> alloc;
    Log *sentinel;
    alignas(CACHE_LINE_SIZE)
    std::mutex tail_mtx;
    Log* tail;
    Log *heads[NODE_COUNT];
    alignas(CACHE_LINE_SIZE)
    std::mutex head_mtxs[NODE_COUNT] = {};

public:

    LogBuffer() {
        sentinel = get_new_log();
        sentinel->produce(false);
        tail = sentinel;
        for (int i=0; i < NODE_COUNT; i++)
            heads[i] = sentinel;
    }

    ~LogBuffer() { 
        if (NODE_COUNT > 1) 
#ifdef BLOCK_ALLOCATOR
            alloc.deallocate(sentinel); 
#else
            delete sentinel;
#endif
    }

    Log *get_new_log() {
#ifdef BLOCK_ALLOCATOR
        Log *alloced = alloc.allocate(); 
        if (!alloced)
            return alloced;
        return new (alloced) Log(); 
#else
        return new Log();
#endif
    }

    void consume_log(Log *l) {
        if (l->consume())
#ifdef BLOCK_ALLOCATOR
            alloc.deallocate(l);
#else
            delete l;
#endif
    }

    void produce_tail(Log *l, bool r) {
        std::unique_lock<std::mutex> lk(tail_mtx);
        l->produce(r);
        tail->next.store(l, std::memory_order_release);
        consume_log(tail);
        tail = l;
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    Log *take_head(unsigned nid) {
        auto next = heads[nid]->next.load(std::memory_order_acquire);
        if (!next)
            return nullptr;
        consume_log(heads[nid]);
        heads[nid] = next;
        return next;
    }
};

#endif
