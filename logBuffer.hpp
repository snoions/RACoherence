#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cstdint>
#include <cstddef>

#include "config.hpp"
#include "types.hpp"

//TODO: fold epoch number into Log
class Log {
    size_t size = 0;
    std::atomic<size_t> consumed_count{0};
    uintptr_t invalid_cl[LOGSIZE];

public:
    inline bool write(uintptr_t cl_addr) {
        if (size == LOGSIZE)
            return false;
        invalid_cl[size++] = cl_addr;
        return true;
    }

    inline bool claim() {
        if (size == 0)
            return true;
        if (consumed_count == NODECOUNT-1) {
            size = 0;
            consumed_count = 0;
            return true;
        }
        return false;
    }

    inline bool isFull() {
        return size == LOGSIZE;
    }

    void consume() {
        consumed_count++;
    }

    const uintptr_t *begin() const {
        return &invalid_cl[0];
    }

    const uintptr_t *end() const {
        return &invalid_cl[LOGSIZE];
    }
};

//TODO: use iterators instead indexing by epoch
class LogBuffer {
    Log *logs;
    epoch_t next_epoch = 0;
    //epoch of head
    std::atomic<epoch_t> head{0};

    Log *logFromEpoch(epoch_t ep) const {
        return &logs[ep % LOGBUFSIZE];
    }

public:
    LogBuffer() {
        logs = new Log[LOGBUFSIZE];
    }

    ~LogBuffer() {
        delete[] logs;
    }

    Log *begin() const {
        return &logs[0];
    }

    Log *end() const {
        return &logs[LOGBUFSIZE];
    }

    Log *moveHead() {
        epoch_t h;
        do {
            h = head;
        } while(!head.compare_exchange_strong(h, h+1));
        return logFromEpoch(h+1);
    }

    //returns whether there are more logs to consume
    const Log *consumeTail(epoch_t &tail) {
        if (tail == head) {
            return NULL;
        }
        Log *tail_log = logFromEpoch(tail);
        tail_log->consume();
        tail++;
        return tail_log;
    }

    Log *getHead() const {
        return logFromEpoch(head);
    }
};

#endif
