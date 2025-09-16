#ifndef _LOCAL_CL_TABLE_H_
#define _LOCAL_CL_TABLE_H_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "config.hpp"
#include "CLGroup.hpp"

namespace RACoherence {

constexpr int TABLE_ENTRIES = 1ull << 6;
constexpr int SEARCH_ITERS = 6; // only look 6 places
constexpr size_t GROUP_LEN_MIN = 4; //only saves ranges of at least 4 cache line groups

class LocalCLTable {
  /** Each entry here can store 16 cache lines. */

    cl_group_t table[TABLE_ENTRIES] = {};

    int length_entry_count = 0; //TODO: change to group count
    struct EntryBuffer {
        cl_group_index_t begin_index = 0;
        uint64_t begin_mask = 0;
        size_t mid_length = 0;
        uint64_t end_mask = 0;

        // returns true when ptr cannot be inserted into buffer
        inline bool insert(uintptr_t ptr) {
            using namespace cl_group;
            cl_group_index_t index = ptr >> GROUP_SHIFT;
            if (!begin_index)
                begin_index = index;
            if (index == begin_index) {
                int pos = (ptr >> CACHE_LINE_SHIFT) & GROUP_POS_MASK;
                uint64_t mask = 1ull << pos;
                begin_mask |= mask;
                if (begin_mask == FULL_MASK) {
                    begin_index--;
                    mid_length++;
                    begin_mask = 0;
                    assert(begin_index);
                }
                return false;
            } else if (index == begin_index + mid_length + 1) {
                int pos = (ptr >> CACHE_LINE_SHIFT) & GROUP_POS_MASK;
                uint64_t mask = 1ull << pos;
                end_mask |= mask;
                if (end_mask == FULL_MASK) {
                    mid_length++;
                    end_mask = 0;
                }
                return false;
            } else if (index > begin_index && index < begin_index + mid_length + 1)
                return false;
            else
                return true;
        }
    } buffer;

    inline bool insert_length(cl_group_index_t group_index, size_t length) {
        using namespace cl_group;
        assert(length <= GROUP_LEN_MAX);
        cl_group_t entry = group_index | (length << GROUP_INDEX_SHIFT) | TYPE_MASK;

        //scan table for overlaps
        bool inserted = false;
        for(int i = 0; i < TABLE_ENTRIES; i++) {
          cl_group_t val = table[i];
          uint64_t val_index = val & GROUP_INDEX_MASK;
          if (!val && !inserted) {
              table[i] = entry;
              inserted = true;
          } else if (val && val_index >= group_index && val_index < group_index + length) {
            //TODO: deal with the case when val is length-based
                assert(!is_length_based(val));
                if (inserted)
                    table[i] = 0;
                else {
                    table[i] = entry;
                    inserted = true;
                }
          }
        }

        if (inserted)
            length_entry_count++;
        return !inserted;
    }

    inline bool insert_mask(uintptr_t group_index, uint64_t mask) {
        using namespace cl_group;
        //alternatively starting searching from 0
        int tableindex = group_index & (TABLE_ENTRIES - 1);
        for(int i = 0; i < SEARCH_ITERS; i++) {
            uint64_t value = table[tableindex];
            assert(!is_length_based(value));
            if (value == 0) {
                table[tableindex] = group_index | (mask << GROUP_INDEX_SHIFT);
                return false;
            } else if ((value & GROUP_INDEX_MASK) == group_index) {
                table[tableindex] = value | (mask << GROUP_INDEX_SHIFT); // add this bit
                return false;
            }
            tableindex = (tableindex + 1) & (TABLE_ENTRIES -1);
        }
        //Table is full...clear and restart
        return true;
    }


public: 
    /**
     * The insert function returns true if the table was full and
     * insertion was not possible.
     */
    inline bool insert(uintptr_t ptr) {
        using namespace cl_group;
#ifdef LOCAL_CL_TABLE_BUFFER
        if (buffer.insert(ptr)) {
            if (dump_buffer_to_table())
                return true;
            bool full = buffer.insert(ptr);
            assert(!full);
        }
        return false;
#else
        cl_group_index_t index = ptr >> GROUP_SHIFT;
        int pos = (ptr >> CACHE_LINE_SHIFT) & GROUP_POS_MASK;
        uint64_t mask = 1ull << pos;
        return insert_mask(index, mask);
#endif
    }

    inline bool range_insert(uintptr_t &begin, uintptr_t end) {
        //TODO: optimize
        assert(begin <=end);
        for (; begin < end + CACHE_LINE_SIZE; begin+= CACHE_LINE_SIZE)
            if (insert(begin))
                return true;
        return false;
    }

    // returns whether table is full
    inline bool dump_buffer_to_table() {
        using namespace cl_group;
        if (buffer.begin_mask) {
            if (insert_mask(buffer.begin_index, buffer.begin_mask))
                return true;
            buffer.begin_mask = 0;
        }
        if (buffer.end_mask) {
            if (insert_mask(buffer.begin_index + buffer.mid_length + 1, buffer.end_mask))
                return true;
            buffer.end_mask = 0;
        }
        if (buffer.mid_length) {
            if (buffer.mid_length < GROUP_LEN_MIN) {
                for (;buffer.mid_length > 0; buffer.mid_length--) {
                    if (insert_mask(buffer.begin_index + buffer.mid_length, FULL_MASK))
                        return true;
                }
            } else {
                unsigned length = std::max(buffer.mid_length, GROUP_LEN_MAX);
                if (insert_length(buffer.begin_index + 1, buffer.mid_length))
                    return true;
                buffer.mid_length -= length;
            }
        }
        if (buffer.begin_index)
            buffer.begin_index = 0;
        return false;
    }

    inline int get_length_entry_count() {
        return length_entry_count;
    }

    inline cl_group_t *begin() {
        return &table[0];
    }

    inline cl_group_t *end() {
        return &table[TABLE_ENTRIES];
    }

    inline void clear_table() {
        length_entry_count = 0;
        memset(table, 0, sizeof(table));
    }

};

//constexpr unsigned GROUP_SHIFT = CACHELINE_SHIFT + 6; //group of 64
//constexpr unsigned GROUP_INTERNAL_INDEX_MASK = 63;
//struct LocalCLTable {
//  /**
//   * The insert function returns true if the table was full and
//   * insertion was not possible.
//   */
//  
//  bool insert(void *address) {
//    uintptr_t ptr = ((uintptr_t) address) >> CACHELINE_SHIFT;
//    uint32_t val = ptr & GROUP_INDEX_MASK;
//    uint64_t valmask = 1ULL << val;
//    uintptr_t tablevalue = ((uintptr_t) address) >> GROUP_SHIFT;
//    int tableindex = tablevalue & (TABLE_ENTRIES - 1);
//    for(int i = 0; i < SEARCH_ITERS; i++) {
//      uint64_t value = table[tableindex].key;
//      uint64_t masked = value & GROUP_INDEX_MASK;
//      if (value == 0) {
//        table[tableindex] = { tablevalue, valmask };
//        return false;
//      } else if (masked == tablevalue) {
//        table[tableindex].value |= valmask; // add this bit
//        return false;
//      }
//      tableindex++;
//    }
//    //Table is full...clear and restart
//    return true;
//  }
//
//  void iterate() {
//    //Todo
//  }
//  
//  void reset() {
//    memset(table, 0, sizeof(table));
//  }
//
//  /** Each entry here can store 16 cache lines. */
//  
//  TableEntry table[TABLE_ENTRIES] = {};
//}
//

} // RACoherence

#endif
