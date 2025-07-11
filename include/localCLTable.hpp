#pragma once
#include <cstdint>
#include <cstring>

#include "config.hpp"
#include "maskedPtr.hpp"

constexpr int TABLE_ENTRIES = 1ull << 6;
constexpr int SEARCH_ITERS = 6; // only look 6 places

using namespace masked_ptr;

struct LocalCLTable {
  /**
   * The insert function returns true if the table was full and
   * insertion was not possible.
   */
  
  inline bool insert(uintptr_t address) {
    uintptr_t ptr = address >> CACHE_LINE_SHIFT;
    uint32_t val = ptr & INDEX_MASK;
    uint64_t valmask = 1ULL << (val + PTR_SHIFT);
    uintptr_t tablevalue = address >> GROUP_SHIFT;
    //alternative starting searching from 0
    int tableindex = tablevalue & (TABLE_ENTRIES - 1);
    for(int i = 0; i < SEARCH_ITERS; i++) {
      uint64_t value = table[tableindex];
      uint64_t masked = value & PTR_MASK;
      if (value == 0) {
        table[tableindex] = tablevalue | valmask;
        return false;
      } else if (masked == tablevalue) {
        table[tableindex] = value | valmask; // add this bit
        return false;
      }
      tableindex++;
    }
    //Table is full...clear and restart
    return true;
  }

  inline masked_ptr_t *begin() {
    return &table[0];
  }

  inline masked_ptr_t *end() {
    return &table[TABLE_ENTRIES];
  }

  inline void clear() {
    memset(table, 0, sizeof(table));
  }

  /** Each entry here can store 16 cache lines. */

  masked_ptr_t table[TABLE_ENTRIES] = {};
};
