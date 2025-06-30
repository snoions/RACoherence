#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <iostream>
#include <mutex>

#include "config.hpp"

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

    unsigned is_produced() {
        return ref_cn > 0;
    }

    void produce(bool r) {
        is_rel = r;
        ref_cn = 2*NODE_COUNT-1; //tail + (NODE_COUNT-1) heads + (NODE_COUNT-1) user refs
    }

    unsigned consume() {
        assert(is_produced());
        auto prev = ref_cn.fetch_sub(1, std::memory_order_relaxed);
        return prev-1 == 0;
    }

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
        return entries.begin();
    }

    const_iterator end() const {
        return entries.end();
    }
};

class alignas(CACHE_LINE_SIZE) LogBuffer {

    Log *sentinel;
    std::mutex tail_mtx;
    Log* tail;
    Log *heads[NODE_COUNT];
    //TODO: put locks on separate cache lines
    std::mutex head_mtxs[NODE_COUNT] = {};

public:

    LogBuffer() {
        sentinel = get_new_log();
        sentinel->produce(false);
        tail = sentinel;
        for (int i=0; i < NODE_COUNT; i++)
            heads[i] = sentinel;
    }

    ~LogBuffer() { if (NODE_COUNT > 1) delete sentinel; }

    Log *get_new_log() {
        //TODO: allocate from a dedicated buffer from CXL hardware coherent region
        return new Log(); 
    }

    void produce_tail(Log *l, bool r) {
        std::unique_lock<std::mutex> lk(tail_mtx);
        l->produce(r);
        tail->next.store(l, std::memory_order_release);
        if (tail->consume())
            delete tail;
        tail = l;
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    Log *take_head(unsigned nid) {
        auto next = heads[nid]->next.load(std::memory_order_acquire);
        if (!next)
            return nullptr;
        if (heads[nid]->consume())
            delete heads[nid];
        heads[nid] = next;
        return next;
    }

    void consume_head(Log *l) {
        if (l->consume())
            delete l;
    }
};

#endif
