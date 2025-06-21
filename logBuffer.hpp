#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <iostream>
#include <thread>

#include "unistd.h"

#include "config.hpp"
#include "logger.hpp"
#include "vectorClock.hpp"

using bufpos_t = unsigned;

inline bufpos_t next_pos(bufpos_t pos) {
    return (pos+1) % (LOG_BUF_SIZE * 2);
}

extern thread_local unsigned node_id;
extern thread_local unsigned user_id;

class alignas(CACHE_LINE_SIZE) Log {
    using Data = std::array<virt_addr_t, LOG_SIZE>;
    using iterator = Data::iterator;
    using const_iterator = Data::const_iterator;

    //FIXME: modify pos and status atomically
    // status = 0 => may write
    // status = n in {1 .. NODE_COUNT-1} => published, yet to be consumed by n nodes
    // status = NODE_COUNT => to be written
    // maybe use shared_mutex instead
    std::atomic<unsigned> status{0};
    std::atomic<unsigned> ids[NODE_COUNT] = {};
    // saving buffer position here avoids having to check buffer head when taking tail
    bufpos_t pos = 0;
    std::array<uintptr_t, LOG_SIZE> entries;
    size_t size = 0;
    // represents a release write
    bool is_rel;

public:
    
    inline bool write(uintptr_t cl_addr) {
        if (size == LOG_SIZE)
            return false;
        entries[size++] = cl_addr;
        return true;
    }

    inline bool published(unsigned s) {
        return s > 0 && s < NODE_COUNT;
    }

    inline bool prepare_write(bufpos_t p) {
        unsigned expected = 0;
        if (!status.compare_exchange_strong(expected, NODE_COUNT, std::memory_order_relaxed, std::memory_order_relaxed))
            return false;
        //setting status first avoids race on other variables
        size = 0;
        pos = p;
        ids[node_id] = {};
        return true;
    }

    bool is_release() {
        return is_rel;
    }

    bool try_prepare_consume(bufpos_t expected_pos) {
        unsigned s = status.load(std::memory_order_acquire);
        if (!published(s) || pos != expected_pos)
            return false;
        return true;
    }
 
    void prepare_consume(bufpos_t expected_pos) {
        unsigned s;
        do {
            s = status.load(std::memory_order_acquire);
        }while(!published(s) || pos != expected_pos);
    }

    void consume() {
        auto s = status.fetch_sub(1, std::memory_order_relaxed);
        assert(!ids[node_id]);
        assert(published(s));
        ids[node_id] = user_id;
    }

    void publish(bool is_r) {
        is_rel = is_r;
        auto s = status.fetch_sub(1, std::memory_order_release);
        assert(s==NODE_COUNT);
    }

    const_iterator begin() const {
        return entries.begin();
    }

    const_iterator end() const {
        return entries.end();
    }
};

class alignas(CACHE_LINE_SIZE) LogBuffer {
    using Data = std::array<Log, LOG_BUF_SIZE>;
    using iterator = Data::iterator;

    bufpos_t head = 0;
    //TODO: ensure locks are on separate cache lines
    std::mutex head_mtx;
    bufpos_t tails[NODE_COUNT] = {0};
    std::mutex tail_mtxs[NODE_COUNT] = {};
    Data logs;

    inline Log *log_from_index(bufpos_t idx) {
        return &logs[idx % LOG_BUF_SIZE];
    }

public:

    inline iterator begin() {
        return logs.begin();
    }

    inline iterator end() {
        return logs.end();
    }

    Log *take_head() {
        std::lock_guard<std::mutex> g(head_mtx);
        Log *head_log = log_from_index(head);
        if (!head_log->prepare_write(head))
            return NULL;
        head = next_pos(head);
        return head_log;
    }

    std::mutex &get_tail_mutex(unsigned nid) {
        return tail_mtxs[nid];
    }

    Log *take_tail(unsigned nid) {
        Log *tail = log_from_index(tails[nid]);
        tail->prepare_consume(tails[nid]);
        tails[nid] = next_pos(tails[nid]);
        return tail;
    }

    Log *try_take_tail(unsigned nid) {
        Log *tail = log_from_index(tails[nid]);
        if (!tail->try_prepare_consume(tails[nid]))
            return NULL;
        tails[nid] = next_pos(tails[nid]);
        return tail;
    }
};

#endif
