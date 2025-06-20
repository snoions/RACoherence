#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

// this version could dead lock when buffer wraps around, also slightly worse performance when it does not
//
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <shared_mutex>
#include <iostream>

#include "unistd.h"

#include "config.hpp"
#include "logger.hpp"
#include "vectorClock.hpp"

using bufpos_t = unsigned;

inline bufpos_t next_pos(bufpos_t pos) {
    return (pos+1) % (LOG_BUF_SIZE * 2);
}

extern thread_local unsigned node_id;

class alignas(CACHE_LINE_SIZE) Log {
    using Data = std::array<virt_addr_t, LOG_SIZE>;
    using iterator = Data::iterator;
    using const_iterator = Data::const_iterator;

    std::shared_mutex mtx;
    // status = 0 => may write
    // status = n in {1 .. NODE_COUNT-1} => published, yet to be consumed by n nodes
    // maybe use shared_mutex instead
    unsigned status = 0;
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

    bool is_release() {
        return is_rel;
    }

    inline bool prepare_write(bufpos_t p) {
        mtx.lock();
        if (status !=0) {
            mtx.unlock();
            return false;
        }
        size = 0;
        pos = p;
        return true;
    }

    bool prepare_consume(bufpos_t expected_pos) {
        mtx.lock_shared();
        if (status==0 || pos != expected_pos) {
            mtx.unlock_shared();
            return false;
        }
        return true;
    }
 
    bool try_prepare_consume(bufpos_t expected_pos) {
        if (!mtx.try_lock_shared())
            return false;
        if (status==0 || pos != expected_pos) {
            mtx.unlock_shared();
            return false;
        }
        return true;
    }

    void consume() {
        status--;
        mtx.unlock_shared();
    }

    void publish(bool is_r) {
        is_rel = is_r;
        status = NODE_COUNT-1;
        mtx.unlock();
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

    //TODO: ensure mutexes are on separate cache lines
    bufpos_t head = 0;
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
        if (!tail->prepare_consume(tails[nid]))
            return NULL;
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
