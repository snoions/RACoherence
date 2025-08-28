#ifndef _USER_H_
#define _USER_H_

#include "flushUtils.hpp"
#include "config.hpp"
#include "threadOps.hpp"
#include "vectorClock.hpp"
#include "CXLSync.hpp"

extern thread_local ThreadOps *thread_ops;

// Should be power of two
constexpr uintptr_t CXL_NHC_RANGE = 1ull << 30;
constexpr uintptr_t CXL_HC_RANGE = 1ull << 20;
constexpr uintptr_t CXL_SYNC_RANGE = 1ull << 4;

// CXLPool should be in CXL-NHC memory
struct CXLPool {

#ifdef PROTOCOL_OFF
    // assume these are cache coherent
    alignas(CACHE_LINE_SIZE) std::mutex mutexes[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) std::atomic<char> atomic_data[CXL_SYNC_RANGE];
#else
    alignas(CACHE_LINE_SIZE) CXLMutex mutexes[CXL_SYNC_RANGE];
    alignas(CACHE_LINE_SIZE) CXLAtomic<char> atomic_data[CXL_SYNC_RANGE];
#endif
    alignas(CACHE_LINE_SIZE) char data[CXL_NHC_RANGE];
};

class User {
    // CXL mem shared data
    CXLPool &cxl_pool;

    unsigned node_id;
    // user stats
    unsigned write_count = 0;
    unsigned read_count = 0;
    unsigned invalidate_count = 0;
    unsigned blocked_count = 0;

    void handle_store(char *addr, char val);

    char handle_load(char *addr);

    void handle_store_raw(char *addr, char val);

    char handle_load_raw(char *addr);

public:
    User(CXLPool &pool, unsigned nid): cxl_pool(pool), node_id(nid){}

    void run();
};

#endif
