#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>

namespace RACoherence {

constexpr uintptr_t PAGE_SIZE = 1ull << 12; // 4KB
constexpr uintptr_t CACHE_LINE_SIZE = 64;
constexpr uintptr_t CACHE_LINE_SHIFT = 6; // log(CACHE_LINE_SIZE)
constexpr uintptr_t CACHE_LINE_MASK = CACHE_LINE_SIZE-1;
constexpr uintptr_t CACHE_LINES_PER_PAGE = PAGE_SIZE / CACHE_LINE_SIZE;
// assuming 64-bit platform
constexpr int VIRTUAL_ADDRESS_BITS = 48;

constexpr unsigned NODE_COUNT = 4;
constexpr unsigned WORKER_PER_NODE = 3;
constexpr unsigned TOTAL_OPS = 10000 * (1ull << 6);

// Should be power of two
constexpr uintptr_t CXL_NHC_RANGE = 1ull << 30;
constexpr uintptr_t CXL_HC_RANGE = 1ull << 22;
constexpr uintptr_t CXL_SYNC_RANGE = 1ull << 4;

// whether to collect statistics
//#define STATS(s) {s;}
#define STATS(s)

// user threads consume logs to unblock itself, at the expense of contention with cache agent
//#define USER_HELP_CONSUME

// thread clock merges with location clock instead of overwriting it, allows release store to be outside of location clock's critical section
//#define LOCATION_CLOCK_MERGE

// turn off RACoherence protocol, use raw stores and loads
//#define PROTOCOL_OFF

// simulate CXL memory with remote NUMA nodes
//#define USE_NUMA

// consumers invalidate eagerly
#define EAGER_INVALIDATE

// pin each cache agent to a core
#define CACHE_AGENT_AFFINITY

// use buffer in local cl tables
//#define LOCAL_CL_TABLE_BUFFER

// use dlmalloc for as allocator for CXL hardware coherent memory
//#define HC_USE_DLMALLOC

// use locks instead of atomics in workload
#define WORKLOAD_USE_LOCKS

// sequential workload
#define WORKLOAD_TYPE SeqWorkLoad
// random workload
//#define WORKLOAD_TYPE RandWorkLoad

} // RACoherence

#endif
