#ifndef _MICROBENCH_H_
#define _MICROBENCH_H_

#include "cxlSync.hpp"
#include "config.hpp"

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

// CXLPool should be in CXL-NHC memory
struct CXLPool {

#ifdef PROTOCOL_OFF
    // assume these are cache coherent
    //alignas(CACHE_LINE_SIZE) std::mutex mutexes[CXL_SYNC_RANGE];
    //alignas(CACHE_LINE_SIZE) std::atomic<char> atomic_data[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) CXLMutexRaw mutexes[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) CXLAtomicRaw<char> atomic_data[CXL_SYNC_RANGE];
#else
    alignas(CACHE_LINE_SIZE) CXLMutex mutexes[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) CXLAtomic<char> atomic_data[CXL_SYNC_RANGE];
#endif
    alignas(CACHE_LINE_SIZE) char data[CXL_NHC_RANGE];

    CXLPool(): mutexes{}, atomic_data{} {}
};

class Microbench {
    CXLPool &cxl_pool;
    unsigned node_id;
    unsigned locked_offset;

    // user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;
    unsigned blocked_count = 0;

    void handle_store(char *addr, char val);

    char handle_load(char *addr);

    void handle_store_raw(char *addr, char val);

    char handle_load_raw(char *addr);

    void use_locks(UserOp &op);

public:
    Microbench(CXLPool &pool, unsigned nid): cxl_pool(pool), node_id(nid), locked_offset(CXL_SYNC_RANGE){}

    void run();
};

#endif
