#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cstdint>
#include <cstddef>

#include "config.hpp"
#include "types.hpp"

class Log {
public:
    static const size_t max_size = 1 << 4;
    size_t size = 0;
    size_t consumed_count = 0;
    uintptr_t invalid_cl[max_size];

public:
    inline bool write(uintptr_t cl_addr) {
        //overwrite
        if (consumed_count == NODECOUNT-1) {
            size = 0;
            consumed_count = 0;
        } else if (size == max_size)
            return false;
        invalid_cl[size++] = cl_addr;
        return true;
    }

    inline bool isFull() {
        return size == max_size;
    }

    void consume() {
        consumed_count++;
    }

    const uintptr_t *begin() const {
        return &invalid_cl[0];
    }

    const uintptr_t *end() const {
        return &invalid_cl[max_size];
    }
};

//TODO: use iterators instead indexing by epoch
class LogBuffer {
    static const size_t max_size = 1 << 4;
    Log *logs;
    epoch_t next_epoch = 0;
    //epoch of head
    std::atomic<epoch_t> head{0};

    Log *begin() const {
        return &logs[0];
    }

    Log *end() const {
        return &logs[max_size];
    }

    Log *logFromEpoch(epoch_t ep) const {
        return &logs[ep % max_size];
    }

public:
    LogBuffer() {
        logs = new Log[max_size];
    }

    ~LogBuffer() {
        delete[] logs;
    }

    epoch_t moveHead() {
        unsigned h;
        do {
            h = head;
        } while(!head.compare_exchange_strong(h, h+1));
        return h+1;
    }

    //returns if there are more logs to consume
    const Log *consumeTail(epoch_t &tail) {
        if (tail >= head) {
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
