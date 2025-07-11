#ifndef _LOG_BUFFER_H_
#define _LOG_BUFFER_H_

#include <atomic>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <mutex>

#include "maskedPtr.hpp"
#include "config.hpp"

constexpr unsigned LOG_SIZE = 1ull << 6;
constexpr unsigned LOG_BUF_SIZE = 1ull << 10;

class alignas(CACHE_LINE_SIZE) Log {
    using Entry = masked_ptr_t;
    using Data = std::array<Entry, LOG_SIZE>;
    using iterator = Data::iterator;
    using const_iterator = Data::const_iterator;

    struct SPair {
        unsigned c;
        bool p;
        inline SPair () = default;
        inline SPair (unsigned s): c(s &~(1<<31)), p(s & (1<<31)) {}
        inline SPair (unsigned co, bool pa): c(co), p(pa) {}
    };
 
    inline unsigned to_status(SPair p) {  return p.c | ((unsigned)p.p<<31); }

    // bits 0-30 stores how many nodes still need to produce/consume the log
    //  = NODE_COUNT => log to be produced, producer has exclusive ownership
    //  = n in [1 .. NODE_COUNT-1] => log to be consumed by n nodes, consumer has shared ownership
    // bit 31 stores parity bit from the buffer, in case the tail wraps around
    // default parity must be opposite of BufPos's default parity
    std::atomic<unsigned> status {to_status(SPair{0, false})};
    std::array<uintptr_t, LOG_SIZE> entries;
    size_t size = 0;
    // whether is a release write
    bool is_rel;


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

    bool is_release() { return is_rel; }

    inline bool try_prepare_consume(bool par) {
        auto cp = SPair(status.load(std::memory_order_acquire));
        if (!produced(cp.c) || cp.p != par)
            return false;
        return true;
    }

    inline void prepare_consume(bool par) { 
        SPair cp;
        do {
            cp = SPair(status.load(std::memory_order_acquire));
        } while (!produced(cp.c) || cp.p != par);
    }

    void consume();

    void produce(bool is_r);

    const_iterator begin() const { return &entries[0]; }

    const_iterator end() const { return &entries[size]; }
};

// carry a parity bit along with index in the buffer to signal
// when a tail wraps around
struct BufPos {
    unsigned idx = 0;
    bool par = true;

    inline void next() {
        idx = (idx+1) % LOG_BUF_SIZE;
        if (idx == 0)
            par = !par;
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

    inline Log &log_from_index(unsigned idx) { return logs[idx]; }

public:

    iterator begin() { return logs.begin(); }

    iterator end() { return logs.end(); }

    Log *take_tail();

    std::mutex &get_head_mutex(unsigned nid) { return head_mtxs[nid]; }

    Log &take_head(unsigned nid);

    Log *try_take_head(unsigned nid);
};

#endif
