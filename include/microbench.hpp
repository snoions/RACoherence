#ifndef _MICROBENCH_H_
#define _MICROBENCH_H_

#include "cxlSync.hpp"
#include "config.hpp"

#include <random>

namespace RACoherence {

static constexpr size_t CXL_POOL_DATA_SIZE = CXL_NHC_RANGE >> 4;

constexpr unsigned SEQ_OP_FACTOR = 3;
// ratio of plain operations to acq/rel operations, needs to be power of two
constexpr uintptr_t PLAIN_ACQ_RLS_RATIO = 1ull << 8;

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

class SeqWorkLoad {
public:
    inline UserOp getNextOp(unsigned index) {
        //assume all atomic is acquire release for now
        bool is_acq_rel = (index & (PLAIN_ACQ_RLS_RATIO-1)) == 0;
        bool is_load = (index % SEQ_OP_FACTOR) % 2 == 0;
        size_t range = is_acq_rel ? CXL_SYNC_RANGE: CXL_POOL_DATA_SIZE;
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

    //inline void setRandOp(unsigned i) {
    //    bool is_acq_rel = (fast_rand() & (PLAIN_ACQ_RLS_RATIO-1)) == 0;
    //    bool is_load = (fast_rand() % 2) == 0;
    //    size_t range = is_acq_rel ? CXL_SYNC_RANGE: CXL_POOL_DATA_SIZE;
    //    size_t offset = fast_rand() & (range-1);
    //    OpType t;
    //    if (is_acq_rel)
    //        t = is_load ? OP_LOAD_ACQ: OP_STORE_RLS;
    //    else
    //        t = is_load ? OP_LOAD: OP_STORE;
    //    op_buffer[i].type = t;
    //    op_buffer[i].offset = offset;
    //}

public:
    RandWorkLoad() {
	int step = CXL_POOL_DATA_SIZE/OP_BUFFER_MAX;
	if (!step)
	    step = 1;
	SeqWorkLoad seq;
        for (int i = 0; i < OP_BUFFER_MAX; i++) {
            UserOp op = seq.getNextOp(i*step);
	    // Fisher-Yates Shuffle
            size_t j = rand() % (i + 1);
            op_buffer[i] = op_buffer[j];
            op_buffer[j] = op;
        }
    }

    inline UserOp getNextOp(unsigned index) {
        assert(index < TOTAL_OPS);
        return op_buffer[index & (OP_BUFFER_MAX-1)];
    }
};

// CXLPool should be in CXL-NHC memory
struct CXLPool {
    alignas(CACHE_LINE_SIZE) CXLMutex mutexes[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) CXLAtomic<char> atomic_data[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) char data[CXL_POOL_DATA_SIZE];

    CXLPool(): mutexes{}, atomic_data{} {}
};

extern thread_local ThreadOps *thread_ops;

class Microbench {
    CXLPool &cxl_pool;
    WORKLOAD_TYPE &workload;
    unsigned node_id;
    unsigned thread_id;
    unsigned locked_offset;

    // user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;
    unsigned blocked_count = 0;

    void handle_store(char *addr, char val);

    char handle_load(char *addr);

    void use_locks(UserOp &op);

public:
    Microbench(CXLPool &pool, WORKLOAD_TYPE &wl): cxl_pool(pool), workload(wl), node_id(thread_ops->get_node_id()), thread_id(thread_ops->get_thread_id()), locked_offset(CXL_SYNC_RANGE){}

    void run();
};

} // RACoherence

#endif
