#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>

namespace RACoherence {

constexpr uintptr_t PAGE_SIZE = 1ull << 12; // 4KB
constexpr uintptr_t CACHE_LINE_SIZE = 64;
constexpr uintptr_t CACHE_LINE_SHIFT = 6;
constexpr uintptr_t CACHE_LINE_MASK = CACHE_LINE_SIZE-1;
//constexpr uintptr_t VIRTUAL_CL_GRANULARITY_SHIFT = 1; // reasonable range is 0 to 5
constexpr uintptr_t VIRTUAL_CL_GRANULARITY_SHIFT = 0; // reasonable range is 0 to 5
constexpr uintptr_t VIRTUAL_CL_GRANULARITY = 1ull << VIRTUAL_CL_GRANULARITY_SHIFT;
constexpr uintptr_t VIRTUAL_CL_SIZE = CACHE_LINE_SIZE * VIRTUAL_CL_GRANULARITY;
constexpr uintptr_t VIRTUAL_CL_SHIFT = CACHE_LINE_SHIFT + VIRTUAL_CL_GRANULARITY_SHIFT; // log(VIRTUAL_CL_SIZE)
constexpr uintptr_t VIRTUAL_CL_MASK = VIRTUAL_CL_SIZE-1;
// assuming 64-bit platform
constexpr int VIRTUAL_ADDRESS_BITS = 48;

constexpr unsigned WORKER_PER_NODE = 3;
constexpr unsigned TOTAL_OPS = 10000 * (1ull << 6); // Should be power of two
constexpr uintptr_t CXL_NHC_START = 1ull << (VIRTUAL_ADDRESS_BITS-1); // should start at the highest virtual bit for easy comparison
constexpr uintptr_t CXL_NHC_RANGE = 1ull << 34;
constexpr uintptr_t CXL_HC_RANGE = 1ull << 24;
constexpr uintptr_t CXL_SYNC_RANGE = 1ull << 4;

// NUMA node that program runs on and makes normal allocations from
#ifndef LOCAL_NUMA_NODE_ID
#define LOCAL_NUMA_NODE_ID 1
#endif
// NUMA node containing CXL memory
#define CXL_NUMA_NODE_ID 2

// path to the file interface exposing wbinvd
#define WBINVD_PATH "/proc/wbinvd"
// number of cache line groups for which a wbinvd is faster than invalidating with clflushopt + mfence
#define WBINVD_THRESHOLD (2 << 18)

#define NODE_COUNT 8

// whether to collect statistics
//#define STATS(s) {s;}
#define STATS(s)

// user threads consume logs to unblock itself, at the expense of contention with cache agent
//#define USER_HELP_CONSUME

// thread clock merges with location clock instead of overwriting it, allows release store to be outside of location clock's critical section
//#define LOCATION_CLOCK_MERGE

// whether turn off RACoherence protocol and use raw stores and loads
#ifndef PROTOCOL_OFF
#define PROTOCOL_OFF 0
#endif

// whether to remove all flush instructions
#ifndef NO_FLUSH
#define NO_FLUSH 0
#endif

// allocate CXL memory from remote NUMA node
#define CXL_NUMA_MODE

// producers flush eagerly
//#define EAGER_FLUSH

// consumers invalidate eagerly
#define EAGER_INVALIDATE

// pin each cache agent to a core
#define CACHE_AGENT_AFFINITY

// delay publishing until a log is full
//#define DELAY_PUBLISH

// use buffer in local cl tables
//#define LOCAL_CL_TABLE_BUFFER

// number of entries in local cl table
#ifndef LOCAL_CL_TABLE_ENTRIES
#define LOCAL_CL_TABLE_ENTRIES (1 << 6)
#endif

// number of entries searched in local cl table per insertion
#ifndef LOCAL_CL_TABLE_SEARCH_ITERS
#define LOCAL_CL_TABLE_SEARCH_ITERS 5
#endif

// use custom memory pool as allocator for CXL hardware coherent memory
#define HC_USE_CUSTOM_POOL

// use locks instead of atomics in workload
#define WORKLOAD_USE_LOCKS

// sequential workload
#define WORKLOAD_TYPE SeqWorkLoad
// random workload
//#define WORKLOAD_TYPE RandWorkLoad

} // RACoherence

#endif
