#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <mutex>

#include "config.hpp"

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

    BufPos head;
    //TODO: ensure locks are on separate cache lines
    std::mutex head_mtx;
    BufPos tails[NODE_COUNT];
    std::mutex tail_mtxs[NODE_COUNT] = {};
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

    //Alternative without head lock: could perform prepare_produce first. If it successes or fails because the head log has been taken by other producers, then move head, otherwise if it fails to due to head log not being consumed, do not move head
    Log *take_head() {
        std::lock_guard<std::mutex> g(head_mtx);
        Log &head_log = log_from_index(head.idx);
        if (!head_log.prepare_produce(head.par))
            return NULL;
        head.next();
        return &head_log;
    }

    std::mutex &get_tail_mutex(unsigned nid) {
        return tail_mtxs[nid];
    }

    Log &take_tail(unsigned nid) {
        Log &tail = log_from_index(tails[nid].idx);
        tail.prepare_consume(tails[nid].par);
        tails[nid].next();
        return tail;
    }

    Log *try_take_tail(unsigned nid) {
        Log &tail = log_from_index(tails[nid].idx);
        if (!tail.try_prepare_consume(tails[nid].par))
            return NULL;
        tails[nid].next();
        return &tail;
    }
};

#endif
