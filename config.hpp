#ifndef _CONFIG_H_
#define _CONFIG_H_

#include <cstdint>

using virt_addr_t = uint64_t;

constexpr virt_addr_t PAGE_SIZE = 1ull << 12;        // 4KB
constexpr virt_addr_t CACHE_LINE_SIZE = 64;
constexpr virt_addr_t CACHE_LINE_MASK = ~(CACHE_LINE_SIZE-1);
constexpr virt_addr_t CACHE_LINES_PER_PAGE = PAGE_SIZE / CACHE_LINE_SIZE;

constexpr std::size_t NODE_COUNT = 10;
constexpr std::size_t WORKER_PER_NODE = 10;
constexpr std::size_t EPOCH = 100;

constexpr std::size_t LOG_SIZE = 1ull << 10;
constexpr unsigned LOG_BUF_SIZE = 1ull << 6;

constexpr virt_addr_t CXLMEM_RANGE = 1ull << 10; //small size to simulate locality
constexpr virt_addr_t CXLMEM_ATOMIC_RANGE = 1ull << 4;
constexpr virt_addr_t ATOMIC_PLAIN_RATIO = 8;


#endif
