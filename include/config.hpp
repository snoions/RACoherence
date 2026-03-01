#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>

namespace RACoherence {

constexpr uintptr_t PAGE_SIZE = 1ull << 12; // 4KB
constexpr uintptr_t CACHE_LINE_SIZE = 64;
constexpr uintptr_t CACHE_LINE_SHIFT = 6;
constexpr uintptr_t CACHE_LINE_MASK = CACHE_LINE_SIZE-1;
constexpr int VIRTUAL_ADDRESS_BITS = 48;

// number of bits to shift hardware cache line size
// in order to get virtual cache line size, reasonable range is 0 to 5
#ifndef CL_EXPAND_SHIFT
#define CL_EXPAND_SHIFT 0
#endif

constexpr uintptr_t CL_EXPAND_FACTOR = 1ull << CL_EXPAND_SHIFT;
constexpr uintptr_t VIRTUAL_CL_SIZE = CACHE_LINE_SIZE * CL_EXPAND_FACTOR;
constexpr uintptr_t VIRTUAL_CL_SHIFT = CACHE_LINE_SHIFT + CL_EXPAND_SHIFT; // log(VIRTUAL_CL_SIZE)
constexpr uintptr_t VIRTUAL_CL_MASK = VIRTUAL_CL_SIZE-1;
// assuming 64-bit platform

// workload settings
constexpr unsigned WORKER_PER_NODE = 3;
constexpr unsigned TOTAL_OPS = 10000 * (1ull << 6); // Should be power of two
constexpr uintptr_t CXL_NHC_START = 1ull << (VIRTUAL_ADDRESS_BITS-1); // should start at the highest virtual bit for easy comparison
constexpr uintptr_t CXL_NHC_RANGE = 1ull << 34;
constexpr uintptr_t CXL_HC_RANGE = 1ull << 24;
constexpr uintptr_t CXL_SYNC_RANGE = 1ull << 4;
// NUMA nodes with CPU that threads may run on
constexpr unsigned CPU_NUMAS[] = {0, 1};

// NUMA node that program runs on and makes normal allocations from
#ifndef LOCAL_NUMA_NODE_ID
#define LOCAL_NUMA_NODE_ID 0
#endif

// NUMA node containing CXL memory
#define CXL_NUMA_NODE_ID 2

// path to the file interface exposing wbinvd
#define WBINVD_PATH "/proc/wbinvd"
// number of cache line groups for which a wbinvd is faster than invalidating with clflushopt + mfence
#define WBINVD_THRESHOLD (2 << 18)

#ifndef NODE_COUNT
#define NODE_COUNT 8
#endif

// whether to collect statistics
//#define STATS(s) {s}
#define STATS(s)

// user threads consume logs to unblock itself, at the expense of contention with cache agent
#ifndef CONSUME_HELPING
#define CONSUME_HELPING 1
#endif

// thread clock merges with location clock instead of overwriting it
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
#ifndef CXL_NUMA_MODE
#define CXL_NUMA_MODE 1
#endif

// producers writeback cache line eagerly, before accessing the cache line line
#ifndef EAGER_WRITEBACK
#define EAGER_WRITEBACK 0
#endif

// consumers invalidate eagerly
#ifndef EAGER_INVALIDATE
#define EAGER_INVALIDATE 1
#endif

// pin each cache agent to a core
#define CACHE_AGENT_AFFINITY

// publish immediately, can only use eager write back
#ifndef IMMEDIATE_PUBLISH
#define IMMEDIATE_PUBLISH 0
#endif

// delay publishing until a log is full
#ifndef DELAY_PUBLISH
#define DELAY_PUBLISH 0
#endif

// use buffer in local cl tables
//#define LOCAL_CL_TABLE_BUFFER

// save recently accessed cache line in an inline cache
#ifndef INLINE_CACHING
#define INLINE_CACHING 1
#endif

// number of entries in local cl table, must be power of two
#ifndef LOCAL_CL_TABLE_SIZE
#define LOCAL_CL_TABLE_SIZE 64
#endif

// number of entries searched in local cl table per insertion
#ifndef LOCAL_CL_TABLE_SEARCH_ITERS
#define LOCAL_CL_TABLE_SEARCH_ITERS 5
#endif

#ifndef LOG_ENTRY_TOTAL
#define LOG_ENTRY_TOTAL 4096
#endif

// number of entries in a log, cannot be smaller than LOCAL_CL_TABLE_ENTRIES
#ifndef LOG_SIZE
#define LOG_SIZE 64
#endif

// number of logs in a per-node log buffer, must be power of two
#define LOG_COUNT (LOG_ENTRY_TOTAL/LOG_SIZE)

// use custom memory pool as allocator for CXL hardware coherent memory
//#define HC_USE_CUSTOM_POOL

// use locks instead of atomics in workload
#define WORKLOAD_USE_LOCKS

// sequential workload
#define WORKLOAD_TYPE SeqWorkLoad
// random workload
//#define WORKLOAD_TYPE RandWorkLoad

} // RACoherence

#endif
