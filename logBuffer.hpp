#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>

#include "config.hpp"

constexpr unsigned LOG_SIZE = 1ull << 6;
constexpr unsigned LOG_BUF_SIZE = 1ull << 10;

// carry a parity bit along with index in the buffer to signal
// when a tail wraps around
struct BufPos {
    unsigned idx = 0;
    bool par = true;

    void next() {
        idx = (idx+1) % LOG_BUF_SIZE;
        if (idx == 0)
            par = !par;
    }
};

class alignas(CACHE_LINE_SIZE) Log {
    using Entry = virt_addr_t;
    using Data = std::array<Entry, LOG_SIZE>;
    using iterator = Data::iterator;
    using const_iterator = Data::const_iterator;

    // bits 0-30 stores how many nodes still need to produce/consume the log
    //  = NODE_COUNT => log to be produced, producer has exclusive ownership
    //  = n in [1 .. NODE_COUNT-1] => log to be consumed by n nodes, consumer has shared ownership
    // bit 31 stores parity bit from the buffer, in case the tail wraps around
    // default parity must be opposite of BufPos's default parity
    std::atomic<unsigned> status {to_status({0, false})};
    std::array<uintptr_t, LOG_SIZE> entries;
    size_t size = 0;
    // whether is a release write
    bool is_rel;

    inline constexpr std::pair<unsigned, bool> from_status(unsigned s) {
        return {s & ~(1<<31), s & (1 <<31)};
    }

    inline constexpr unsigned to_status(std::pair<unsigned, bool> p) {
        return p.first | ((unsigned)p.second<<31);
    }

    inline bool produced(unsigned s) {
        return s > 0 && s < NODE_COUNT;
    }

public:

    inline bool write(uintptr_t cl_addr) {
        if (size == LOG_SIZE)
            return false;
        entries[size++] = cl_addr;
        return true;
    }

    inline bool prepare_produce(bool par) {
        unsigned s = to_status({0, !par});
        if (!status.compare_exchange_strong(s, to_status({NODE_COUNT, par}), std::memory_order_relaxed, std::memory_order_relaxed))
            return false;
        // no data race as thread now has exclusive ownership
        size = 0;
        return true;
    }

    bool is_release() {
        return is_rel;
    }

    bool try_prepare_consume(bool par) {
        auto [c, p] = from_status(status.load(std::memory_order_acquire));
        if (!produced(c) || p != par)
            return false;
        return true;
    }

    void prepare_consume(bool par) { 
        std::pair<unsigned, bool> cp;
        do {
            cp = from_status(status.load(std::memory_order_acquire));
        } while (!produced(cp.first) || cp.second != par);
    }

    void consume() {
        auto [c, _] = from_status(status.fetch_sub(1, std::memory_order_relaxed));
        assert(produced(c));
    }

    void produce(bool is_r) {
        is_rel = is_r;
        auto [c, _] = from_status(status.fetch_sub(1, std::memory_order_release));
        assert(c==NODE_COUNT);
    }

    inline bool produced() {
        return produced(status.load());
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

    alignas(CACHE_LINE_SIZE)
    std::atomic<BufPos> tail;
    BufPos heads[NODE_COUNT];
    alignas(CACHE_LINE_SIZE)
    std::mutex head_mtxs[NODE_COUNT] = {};
    Data logs;

    inline Log &log_from_index(unsigned idx) {
        return logs[idx];
    }

public:

    inline iterator begin() {
        return logs.begin();
    }

    inline iterator end() {
        return logs.end();
    }

    //could also use a lock, performance seems similar
    Log *take_tail() {
        auto t = tail.load(std::memory_order_acquire);
        Log &log = log_from_index(t.idx);
        if (!log.prepare_produce(t.par))
            return NULL;
        t.next();
        tail.store(t,std::memory_order_release);
        return &log;
    }

    std::mutex &get_head_mutex(unsigned nid) {
        return head_mtxs[nid];
    }

    Log &take_head(unsigned nid) {
        Log &log = log_from_index(heads[nid].idx);
        log.prepare_consume(heads[nid].par);
        heads[nid].next();
        return log;
    }

    Log *try_take_head(unsigned nid) {
        Log &log = log_from_index(heads[nid].idx);
        if (!log.try_prepare_consume(heads[nid].par))
            return NULL;
        heads[nid].next();
        return &log;
    }
};

#endif
