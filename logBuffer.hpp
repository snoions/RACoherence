#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <iostream>

#include "config.hpp"

//TODO: use vector clocks
//TODO: allow multiple writers to operate on a buffer

using idx_t = unsigned;

inline idx_t next_index(idx_t idx) {
    return (idx+1) % (LOGBUFSIZE * 2);
}

class Log {
    size_t size = 0;
    //status = 0 => free
    //status = n in {1 .. NODECOUNT-1} => published, yet to be consumed by n nodes
    //status = NODECOUNT => in use by worker
    std::atomic<unsigned> status{0};
    idx_t index = 0;
    uintptr_t invalid_cl[LOGSIZE];

public:
    inline bool write(uintptr_t cl_addr) {
        if (size == LOGSIZE)
            return false;
        invalid_cl[size++] = cl_addr;
        return true;
    }

    inline bool published() {
        return status != 0 && status != NODECOUNT;
    }

    inline bool use(idx_t idx) {
        if (status != 0)
            return false;
        //setting status first avoids race on other variables
        status = NODECOUNT;
        size = 0;
        index = idx;
        return true;
    }

    idx_t getIndex() {
        return index;
    }

    void consume() {
        assert(status > 0);
        status--;
    }
    
    void publish() {
        assert(status ==NODECOUNT);
        status--;
    }

    const uintptr_t *begin() const {
        return &invalid_cl[0];
    }

    const uintptr_t *end() const {
        return &invalid_cl[LOGSIZE];
    }
};

//TODO: use ptr instead indexing by epoch
class LogBuffer {
    Log *logs;
    // idx_t encodes a position in log buffer
    // in range [0, 2*LOGBUFSIZE) to distinguish
    // two copiers who can be at most 
    // LOGBUFSIZE apart
    idx_t head = 0;
    std::mutex lock;

    inline Log *logFromIndex(idx_t idx) {
        return &logs[idx % LOGBUFSIZE];
    }

public:
    LogBuffer() {
        logs = new Log[LOGBUFSIZE];
    }

    ~LogBuffer() {
        delete[] logs;
    }

    inline Log *begin() const {
        return &logs[0];
    }

    inline Log *end() const {
        return &logs[LOGBUFSIZE];
    }

    Log *takeHead() {
        std::lock_guard<std::mutex> guard(lock);
        Log *head_log = logFromIndex(head);
        if (!head_log->use(head))
            return NULL;
        head++;
        return head_log;
    }

    Log *consumeTail(idx_t &idx) {
        Log *tail = logFromIndex(idx);
        if (!tail->published())
            return NULL;
        if (tail->getIndex() < idx)
            return NULL;
        tail->consume();
        idx = next_index(idx);
        return tail;
    }
};

#endif
