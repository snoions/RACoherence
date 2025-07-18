#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>

constexpr uintptr_t PAGE_SIZE = 1ull << 12; // 4KB
constexpr uintptr_t CACHE_LINE_SIZE = 64;
constexpr uintptr_t CACHE_LINE_SHIFT = 6; // log(CACHE_LINE_SIZE)
constexpr uintptr_t CACHE_LINE_MASK = ~(CACHE_LINE_SIZE-1);
constexpr uintptr_t CACHE_LINES_PER_PAGE = PAGE_SIZE / CACHE_LINE_SIZE;
// assuming 64-bit platform
constexpr int VIRTUAL_ADDRESS_BITS = 48;

constexpr unsigned NODE_COUNT = 4;
constexpr unsigned WORKER_PER_NODE = 4;
constexpr unsigned TOTAL_OPS = 10000 * (1ull << 6);

// whether to collect statistics
#define STATS

// whether user threads may consume logs to unblock itself, at the expense of contention with cache agent
#define USER_HELP_CONSUME

// turn off RACoherence protocol, use raw stores and loads
//#define PROTOCOL_OFF

// whether to use sequential workload
//#define SEQ_WORKLOAD
// a constant that is co-prime with PLAIN_ACQ_REL_RATIO to decouple choice of acq/rel with load/store
#define SEQ_OP_FACTOR 3

#endif
