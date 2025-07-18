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
    OpType type;
    size_t offset;
};

class SeqWorkLoad {
public:
    inline UserOp getNextOp(unsigned index) {
        //assume all atomic is acquire release for now
        bool is_acq_rel = (index & (PLAIN_ACQ_REL_RATIO-1)) == 0;
        OpType t;
        if ((index % SEQ_OP_FACTOR) % 2 == 0)
            t = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            t = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t range = is_acq_rel ? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE;
        size_t offset = (OP_ALIGN * index) & (range-1);
        return {t, offset};
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


// RandWorkLoad does not guarantee data-race freedom
// Data are pre-generated to overhead during user execution
class RandWorkLoad {
    static constexpr size_t OP_BUFFER_MAX = 1 << 15;
    UserOp op_buffer[OP_BUFFER_MAX] = {};

    inline void setRandOp(UserOp &op) {
        bool is_acq_rel = (fast_rand() & (PLAIN_ACQ_REL_RATIO-1)) == 0;
        OpType t;
        if ((fast_rand() % 2) == 0)
            t = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            t = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t range = is_acq_rel ? CXLMEM_ATOMIC_RANGE: CXLMEM_RANGE;
        size_t offset = (fast_rand() & (range-1)) & ~(OP_ALIGN-1);
        op.type = t;
        op.offset = offset;
    }

public:
    RandWorkLoad() {
        for (int i = 0; i < OP_BUFFER_MAX; i++)
            setRandOp(op_buffer[i]);
    }

    inline UserOp getNextOp(unsigned index) {
        assert(index < TOTAL_OPS);
        return op_buffer[index & (OP_BUFFER_MAX-1)];
    }
};
#endif
