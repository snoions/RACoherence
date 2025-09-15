#ifndef _MICROBENCH_H_
#define _MICROBENCH_H_

#include "cxlSync.hpp"
#include "config.hpp"

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

// CXLPool should be in CXL-NHC memory
struct CXLPool {
    alignas(CACHE_LINE_SIZE) CXLMutex mutexes[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) CXLAtomic<char> atomic_data[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) char data[CXL_NHC_RANGE];

    CXLPool(): mutexes{}, atomic_data{} {}
};

extern thread_local ThreadOps *thread_ops;

class Microbench {
    CXLPool &cxl_pool;
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
    Microbench(CXLPool &pool): cxl_pool(pool), node_id(thread_ops->get_node_id()), thread_id(thread_ops->get_thread_id()), locked_offset(CXL_SYNC_RANGE){}

    void run();
};

#endif
