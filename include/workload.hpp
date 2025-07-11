#ifndef _WORKLOAD_H_
#define _WORKLOAD_H_

#include "config.hpp"
#include "memLayout.hpp"

// Should be power of 2
constexpr uintptr_t OP_ALIGN = 1ull << 3;
// ratio of plain operations to acq/rel operations, needs to be power of two
constexpr uintptr_t PLAIN_ACQ_REL_RATIO = 1ull << 8;

enum OpType {
    OP_STORE,
    OP_STORE_REL,
    OP_LOAD,
    OP_LOAD_ACQ,
    OP_END
};

struct UserOp {
    OpType op;
    size_t offset;
};

class SeqWorkLoad {
public:
    inline UserOp getNextOp(unsigned index) {
        //assume all atomic is acquire release for now
        bool is_acq_rel = (index & (PLAIN_ACQ_REL_RATIO-1)) == 0;
        OpType op;
        if ((index % SEQ_OP_FACTOR) % 2 == 0)
            op = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            op = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t range = is_acq_rel ? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE;
        size_t offset = (OP_ALIGN * index) & (range-1);
        return {op, offset};
    }
};


// xorshf96
inline unsigned long fast_rand() {          //period 2^96-1
    static unsigned long x=123456789, y=362436069, z=521288629;
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}


// May not be data-race free
class RandWorkLoad { 
public:
    inline UserOp getNextOp(unsigned index) {
        bool is_acq_rel = (fast_rand() & (PLAIN_ACQ_REL_RATIO-1)) == 0;
        OpType op;
        if ((fast_rand() % 2) == 0)
            op = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            op = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t range = is_acq_rel ? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE;
        size_t offset = (fast_rand() & (range-1)) & ~(OP_ALIGN-1);
        return {op, offset};
    }
};
#endif
