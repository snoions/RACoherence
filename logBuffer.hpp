#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <iostream>

#include "config.hpp"
#include "types.hpp"

//TODO: use vector clocks
//TODO: allow multiple writers to operate on a buffer
class Log {
    size_t size = 0;
    //status = 0 => free
    //status = n in {1 .. NODECOUNT-1} => published, yet to be consumed by n nodes
    //status = NODECOUNT => in use by worker
    std::atomic<unsigned> status{0};
    epoch_t epoch = 0;
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

    inline bool use(epoch_t ep) {
        if (status != 0)
            return false;
        //setting status first avoids race on other variables
        status = NODECOUNT;
        size = 0;
        epoch = ep;
        return true;
    }

    epoch_t getEpoch() {
        return epoch;
    }   

    void consume() {
        assert(status > 0);
        status--;
    }
    
    void publish() {
        assert(status ==NODECOUNT );
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
    epoch_t next_epoch = 0;
    std::mutex lock;

    inline Log *logFromEpoch(epoch_t ep) {
        return &logs[ep % LOGBUFSIZE];
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
        Log *head = logFromEpoch(next_epoch);
        if (!head->use(next_epoch))
            return NULL;
        next_epoch++;
        return head;
    }

    Log *consumeTail(epoch_t ep) {
        Log *tail = logFromEpoch(ep);
        if (!tail->published())
            return NULL;
        if (tail->getEpoch() < ep)
            return NULL;
        tail->consume();
        return tail;
    }
};

#endif
