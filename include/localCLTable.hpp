#ifndef _LOCAL_CL_TABLE_H_
#define _LOCAL_CL_TABLE_H_

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "config.hpp"
#include "clGroup.hpp"

namespace RACoherence {

class LocalCLTable {
  /** Each entry here can store 16 cache line units. */

    cl_group_t table[LOCAL_CL_TABLE_ENTRIES] = {};
    int length_entry_count = 0; //TODO: temporary hack, improve later
    struct EntryBuffer {
        uintptr_t cl_addr = 0;
        size_t len = 0;

        // returns true when ptr cannot be inserted into buffer
        inline bool insert(uintptr_t cl_ptr) {
            if (cl_ptr == cl_addr + len) {
                len++;
                return false;
            }
            if (cl_ptr >= cl_addr && cl_ptr < cl_addr + len)
                return false;
            if (len == 0) {
                cl_addr = cl_ptr;
                len = 1;
                return false;
            }
            return true;
        }
    } buffer;

    // returns true if full
    inline bool insert_length(cl_group_idx group_index, size_t length) {
        using namespace cl_group;
        assert(length <= GROUP_LEN_MAX);
        cl_group_t entry = group_index | (length << GROUP_INDEX_SHIFT) | TYPE_MASK;

        //scan table for overlaps
        int insert_pos = -1;
        for(int i = 0; i < LOCAL_CL_TABLE_ENTRIES; i++) {
            cl_group_t val = table[i];
            uint64_t val_index = get_index(val);
            if (!val) {
                if (insert_pos == -1)
                    insert_pos = i;
            } else if (is_length_based(val)) {
                size_t val_length = get_length(val);
                if (auto new_entry = try_coalesce(val_index, group_index, val_length, length)) {
                    entry = new_entry;
                    table[i] = 0;
                    if (insert_pos == -1)
                        insert_pos = i;
                }
            } else if (val_index >= group_index && val_index < group_index + length) {
                table[i] = 0;
                if (insert_pos == -1)
                    insert_pos = i;
            }
        }

        if (insert_pos != -1) {
            table[insert_pos] = entry; 
            length_entry_count++;
        }
        return insert_pos == -1;
    }

    // returns true if full
    inline bool insert_mask(uintptr_t group_index, uint64_t mask) {
        using namespace cl_group;
        //alternatively starting searching from 0
        for(int i = 0; i < LOCAL_CL_TABLE_SEARCH_ITERS; i++) {
            int tableindex = (group_index + i) & (LOCAL_CL_TABLE_ENTRIES - 1);
            uint64_t value = table[tableindex];
            value = value ? value : group_index;
            //assert(!is_length_based(value));
            if ((value & GROUP_INDEX_MASK) == group_index) {
                table[tableindex] = value | (mask << GROUP_INDEX_SHIFT); // add this bit
                return false;
            }
        }
        //Table is full...clear and restart
        return true;
    }


public: 
    /**
     * The insert function returns true if the table was full and
     * insertion was not possible.
     */

    inline bool insert(uintptr_t cl_addr) {
        using namespace cl_group;
#ifdef LOCAL_CL_TABLE_BUFFER
        if (buffer.insert(cl_addr)) {
            if (dump_buffer_to_table())
                return true;
            bool full = buffer.insert(cl_addr);
            assert(!full);
        }
        return false;
#else
        cl_group_idx index = cl_addr >> GROUP_SIZE_SHIFT;
        int pos = cl_addr & GROUP_SIZE_MASK;
        uint64_t mask = 1ull << pos;
        return insert_mask(index, mask);
#endif
    }

//    inline bool insert_may_straddle(uintptr_t ptr, size_t byte_offset) {
//        using namespace cl_group;
//#ifdef LOCAL_CL_TABLE_BUFFER
//        //TODO: handle possible straddling
//        if (buffer.insert(ptr)) {
//            if (dump_buffer_to_table())
//                return true;
//            bool full = buffer.insert(ptr);
//            assert(!full);
//        }
//        return false;
//#else
//        uintptr_t cl_addr = ptr >> CL_UNIT_SHIFT;
//        uintptr_t cl_addr_end = (ptr + byte_offset) >> CL_UNIT_SHIFT;
//        cl_group_idx index = ptr >> GROUP_SHIFT;
//        int pos = cl_addr & GROUP_SIZE_MASK;
//        uint64_t mask;
//        if (cl_addr < cl_addr_end) {
//            if (pos == GROUP_SIZE - 1) {
//                if (insert_mask(index + 1, 1ull))
//                   return true;
//                mask = 1ull << (GROUP_SIZE-1);
//            }
//            else
//                mask = 2ull << pos;
//        } else {
//            mask = 1ull << pos;
//        }
//        return insert_mask(index, mask);
//#endif
//    }

    inline bool range_insert(uintptr_t &begin, uintptr_t &end) {
        assert(begin <=end);
        for (; begin <= end; begin++)
            if (insert(begin))
                return true;
        begin--;
        return false;
	// try to use insert_length if possible currently shows no speedup
        //using namespace cl_group;
	//constexpr size_t GROUP_LEN_MIN = 4; //only saves ranges of at least 4 cache line groups

        //cl_group_idx end_index = end >> GROUP_SIZE_SHIFT;
        //unsigned begin_pos = begin & GROUP_SIZE_MASK;
        //unsigned end_pos = end & GROUP_SIZE_MASK;
        //if(begin_pos) {
        //    cl_group_idx begin_index = begin >> GROUP_SIZE_SHIFT;
        //    uint64_t begin_mask = (FULL_MASK << begin_pos) & FULL_MASK;
        //    if (begin_index == end_index) {
        //        uint64_t end_mask = 1ull << end_pos;
        //        if (insert_mask(begin_index, begin_mask & end_mask))
        //            return true;
        //        begin = end;
        //        return false;
        //    }
        //    if (insert_mask(begin_index, begin_mask))
        //        return true;
        //    begin += GROUP_SIZE - begin_pos;
        //}
        //if (end_pos) {
        //    uint64_t end_mask = 1ull << end_pos;
        //    if (insert_mask(end_index, end_mask))
        //        return true;
        //    end -= end_pos;
        //}
        //cl_group_idx begin_index = begin >> GROUP_SIZE_SHIFT;
        //end_index = end >> GROUP_SIZE_SHIFT;
        //size_t len = end_index - begin_index;
        //if (len < GROUP_LEN_MIN) {
        //    for (; begin_index < end_index; begin_index++) {
        //        if (insert_mask(begin_index, FULL_MASK)) {
        //            begin = begin_index << GROUP_SIZE_SHIFT;
        //            return true;
        //        }
        //    }
        //} else {
        //    while(len) {
        //        unsigned l = len & GROUP_LEN_MAX;
        //        if (insert_length(begin_index, l)) {
        //            begin = begin_index << GROUP_SIZE_SHIFT;
        //            return true;
        //        }
        //        len -= l;
        //        begin_index += l;
        //    }
        //}
        //begin = end;
        //return false;
    }

    // returns whether table is full
    inline bool dump_buffer_to_table() {
         uintptr_t end = buffer.cl_addr + buffer.len;
         bool ret = range_insert(buffer.cl_addr, end);
         buffer.len = end - buffer.cl_addr;
         return ret;
    }

    inline int get_length_entry_count() {
        return length_entry_count;
    }

    inline cl_group_t *begin() {
        return &table[0];
    }

    inline cl_group_t *end() {
        return &table[LOCAL_CL_TABLE_ENTRIES];
    }

    inline void clear_table() {
        length_entry_count = 0;
        memset(table, 0, sizeof(table));
    }

};

} // RACoherence

#endif
