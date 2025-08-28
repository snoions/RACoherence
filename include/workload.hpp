#ifndef _WORKLOAD_H_
#define _WORKLOAD_H_

#include "config.hpp"
#include "user.hpp"

// Should be power of 2
constexpr unsigned SEQ_OP_FACTOR = 3;
// ratio of plain operations to acq/rel operations, needs to be power of two
constexpr uintptr_t PLAIN_ACQ_RLS_RATIO = 1ull << 8;
constexpr size_t OP_BUFFER_MAX = 1 << 15;

enum OpType {
    OP_STORE,
    OP_STORE_RLS,
    OP_LOAD,
    OP_LOAD_ACQ,
    OP_END,
    OP_LOCK,
    OP_UNLOCK
};

struct UserOp {
    OpType type;
    size_t offset;
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

class SeqWorkLoad {
public:
    inline UserOp getNextOp(unsigned index) {
        //assume all atomic is acquire release for now
        bool is_acq_rel = (index & (PLAIN_ACQ_RLS_RATIO-1)) == 0;
        bool is_load = (index % SEQ_OP_FACTOR) % 2 == 0;
        size_t range = is_acq_rel ? CXL_SYNC_RANGE: CXL_NHC_RANGE;
        size_t offset = index & (range-1);
        OpType t;
        if (is_acq_rel)
            t = is_load ? OP_LOAD_ACQ: OP_STORE_RLS;
        else
            t = is_load ? OP_LOAD: OP_STORE;
        return {t, offset};
    }
};

class SeqLockWorkLoad {
    size_t lock_offset = CXL_SYNC_RANGE;
public:
    inline UserOp getNextOp(unsigned index) {
        //assume all atomic is acquire release for now
        bool is_acq_rel = (index & (PLAIN_ACQ_RLS_RATIO-1)) == 0;
        bool is_load = (index % SEQ_OP_FACTOR) % 2 == 0;
        size_t range = is_acq_rel ? CXL_SYNC_RANGE: CXL_NHC_RANGE;
        size_t offset = index & (range-1);
        OpType t;
        if (is_acq_rel) {
            if (lock_offset == CXL_SYNC_RANGE) {
                t = OP_LOCK;
                lock_offset = offset;
            } else {
                t = OP_UNLOCK;
                offset = lock_offset;
                lock_offset = CXL_SYNC_RANGE;
            }
        } else
            t = is_load? OP_LOAD: OP_STORE;
        return {t, offset};
    }
};

// RandWorkLoad does not guarantee data-race freedom
// Data are pre-generated to overhead during user execution
class RandWorkLoad {
    UserOp op_buffer[OP_BUFFER_MAX] = {};

    inline void setRandOp(unsigned i) {
        bool is_acq_rel = (fast_rand() & (PLAIN_ACQ_RLS_RATIO-1)) == 0;
        bool is_load = (fast_rand() % 2) == 0;
        size_t range = is_acq_rel ? CXL_SYNC_RANGE: CXL_NHC_RANGE;
        size_t offset = fast_rand() & (range-1);
        OpType t;
        if (is_acq_rel)
            t = is_load ? OP_LOAD_ACQ: OP_STORE_RLS;
        else
            t = is_load ? OP_LOAD: OP_STORE;
        op_buffer[i].type = t;
        op_buffer[i].offset = offset;
    }

public:
    RandWorkLoad() {
        for (int i = 0; i < OP_BUFFER_MAX; i++)
            setRandOp(i);
    }

    inline UserOp getNextOp(unsigned index) {
        assert(index < TOTAL_OPS);
        return op_buffer[index & (OP_BUFFER_MAX-1)];
    }
};

class RandLockWorkLoad {
    UserOp op_buffer[OP_BUFFER_MAX] = {};
    unsigned lock_offset = CXL_SYNC_RANGE;

    inline void setRandOp(unsigned i) {
        bool is_acq_rel;
        if (i == OP_BUFFER_MAX-1)
            is_acq_rel = lock_offset != CXL_SYNC_RANGE;
        else
            is_acq_rel = (fast_rand() & (PLAIN_ACQ_RLS_RATIO-1)) == 0;
        bool is_load = (fast_rand() % 2) == 0;
        size_t range = is_acq_rel ? CXL_SYNC_RANGE: CXL_NHC_RANGE;
        size_t offset = fast_rand() & (range-1);
        OpType t;
        if (is_acq_rel) {
            if (lock_offset == CXL_SYNC_RANGE) {
                t = OP_LOCK;
                lock_offset = offset;
            } else {
                t = OP_UNLOCK;
                offset = lock_offset;
                lock_offset = CXL_SYNC_RANGE;
            }
        } else
            t = is_load ? OP_LOAD: OP_STORE;
        op_buffer[i].type = t;
        op_buffer[i].offset = offset;
    }

public:
    RandLockWorkLoad() {
        for (int i = 0; i < OP_BUFFER_MAX; i++)
            setRandOp(i);
    }

    inline UserOp getNextOp(unsigned index) {
        assert(index < TOTAL_OPS);
        return op_buffer[index & (OP_BUFFER_MAX-1)];
    }
};
#endif
