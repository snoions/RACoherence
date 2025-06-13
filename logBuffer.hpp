#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>
#include <iostream>

#include "config.hpp"
#include "vectorClock.hpp"

//TODO: use vector clocks
//TODO: allow multiple writers to operate on a buffer


using bufpos_t = unsigned;

inline bufpos_t next_pos(bufpos_t pos) {
    return (pos+1) % (LOGBUFSIZE * 2);
}

class alignas(CACHELINESIZE) Log {
    using Data = std::array<uintptr_t, LOGSIZE>;
    using iterator = Data::iterator;
    using const_iterator = Data::const_iterator;
    //status = 0 => free
    //status = n in {1 .. NODECOUNT-1} => published, yet to be consumed by n nodes
    //status = NODECOUNT => in use by worker
    std::atomic<unsigned> status{0};
    //saving buffer position here avoids having to check buffer head when taking tail
    bufpos_t pos = 0;
    std::array<uintptr_t, LOGSIZE> entries;
    size_t size = 0;

public:
    inline bool write(uintptr_t cl_addr) {
        if (size == LOGSIZE)
            return false;
        entries[size++] = cl_addr;
        return true;
    }

    inline bool published() {
        return status != 0 && status != NODECOUNT;
    }

    inline bool use(bufpos_t p) {
        if (status != 0)
            return false;
        //setting status first avoids race on other variables
        status = NODECOUNT;
        size = 0;
        pos = p;
        return true;
    }

    bufpos_t getPos() {
        return pos;
    }

    void consume() {
        assert(status > 0);
        status--;
    }
    
    void publish() {
        assert(status ==NODECOUNT);
        status--;
    }

    const_iterator begin() const {
        return entries.begin();
    }

    const_iterator end() const {
        return entries.end();
    }
};

class alignas(CACHELINESIZE) LogBuffer {
    using Data = std::array<Log, LOGBUFSIZE>;
    using iterator = Data::iterator;
    Data logs;
    
    bufpos_t head = 0;
    std::mutex head_lock;
    bufpos_t tails[NODECOUNT] = {0};

    inline Log *logFromIndex(bufpos_t idx) {
        return &logs[idx % LOGBUFSIZE];
    }

public:

    inline iterator begin() {
        return logs.begin();
    }

    inline iterator end() {
        return logs.end();
    }

    Log *takeHead() {
        std::lock_guard<std::mutex> guard(head_lock);
        Log *head_log = logFromIndex(head);
        if (!head_log->use(head))
            return NULL;
        head++;
        return head_log;
    }

    Log *consumeTail(unsigned nid) {
        Log *tail = logFromIndex(tails[nid]);
        if (!tail->published())
            return NULL;
        if (tail->getPos() < tails[nid])
            return NULL;
        tail->consume();
        tails[nid] = next_pos(tails[nid]);
        return tail;
    }
};

#endif
