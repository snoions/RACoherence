#include "microbench.hpp"
#include "logger.hpp"

constexpr unsigned SEQ_OP_FACTOR = 3;

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

// RandWorkLoad does not guarantee data-race freedom
// Data are pre-generated to overhead during user execution
class RandWorkLoad {
    static const size_t OP_BUFFER_MAX = 1 << 15;
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

inline void Microbench::handle_store(char *addr, char val) {
    if (thread_ops->check_invalidate(addr)) {
#ifdef STATS
        invalidate_count++;
#endif
    }
    thread_ops->log_store(addr);
    *((volatile char *)addr) = val;
}

inline char Microbench::handle_load(char *addr) {
    if (thread_ops->check_invalidate(addr)) {
#ifdef STATS
        invalidate_count++;
#endif
    }
    return *((volatile char *)addr);
}

inline void Microbench::handle_store_raw(char *addr, char val) {
    do_invalidate(addr);
    invalidate_fence();
#ifdef STATS
    invalidate_count++;
#endif
    *((volatile char *)addr) = val;
    do_flush((char *)addr);
}

inline char Microbench::handle_load_raw(char *addr) {
    do_invalidate(addr);
    invalidate_fence();
#ifdef STATS
    invalidate_count++;
#endif
    return *((volatile char *)addr);
}

inline void Microbench::use_locks(UserOp &op) {
    if (op.type != OP_STORE_RLS && op.type != OP_LOAD_ACQ)
        return;
    if (locked_offset == CXL_SYNC_RANGE) {
        op.type = OP_LOCK;
        locked_offset =op.offset;
    } else {
        op.type = OP_UNLOCK;
        op.offset = locked_offset;
        locked_offset = CXL_SYNC_RANGE;
    }
}

void Microbench::run() {
    WORKLOAD_TYPE workload;
    for (int i =0; i < TOTAL_OPS; i++) {
        UserOp op = workload.getNextOp(i);
#ifdef WORKLOAD_USE_LOCKS
        use_locks(op);
#endif
        //TODO: data-race-free workload based on synchronization (locked region?)
        switch (op.type) {
            case OP_STORE_RLS: {
#ifdef STATS
                write_count++;
#endif
                cxl_pool.atomic_data[op.offset].store(0, std::memory_order_release);
#ifdef PROTOCOL_OFF
                flush_fence();
#endif
                break;
            }
             case OP_STORE: {
#ifdef STATS
                write_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_store_raw(&cxl_pool.data[op.offset], 0);
#else
                handle_store(&cxl_pool.data[op.offset], 0);
#endif
                break;
            }
            case OP_LOAD_ACQ: {
#ifdef STATS
                read_count++;
#endif
                cxl_pool.atomic_data[op.offset].load(std::memory_order_acquire);
                break;
            }
            case OP_LOAD: {
#ifdef STATS
                read_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_load_raw(&cxl_pool.data[op.offset]);
#else
                handle_load(&cxl_pool.data[op.offset]);
#endif
                break;
            }
            case OP_LOCK: {
                cxl_pool.mutexes[op.offset].lock();
                break;
            }
            case OP_UNLOCK: {
                cxl_pool.mutexes[op.offset].unlock();
#ifdef PROTOCOL_OFF
                flush_fence();
#endif
                break;
            }
            default:
                assert("unreachable");
        }
    }

    if(locked_offset != CXL_SYNC_RANGE)
        cxl_pool.mutexes[locked_offset].unlock();
    LOG_INFO("node " << node_id << " thread " << thread_id << " done")
}
