#ifndef _MASKED_PTR_H
#define _MASKED_PTR_H

#include <cstddef>
#include "config.hpp"

namespace RACoherence {
// representation for a group of cache lines
// there are two variants: length and bitmask-based
// length-based represents contiguous groups of cl 
// bitmask-based represents cls wthin a cl group
// length-based: 1 + 25 bit length + 38 bit group index
// bitmask-based: 0 + 9 bit padding + 16 bit mask + 38 bit group index
using cl_group_t = uint64_t;
using cl_group_index_t = uint64_t;

namespace cl_group {

    constexpr uint64_t TYPE_MASK = 1ull << 63;
    constexpr uint64_t GROUP_SIZE_SHIFT = 4; //group of 16 CLs
    constexpr size_t GROUP_SIZE = 1ull << GROUP_SIZE_SHIFT;
    constexpr size_t FULL_MASK = (1ull << GROUP_SIZE) - 1ull;
    constexpr uint64_t GROUP_SHIFT = CACHE_LINE_SHIFT + GROUP_SIZE_SHIFT;
    constexpr uint64_t GROUP_MASK = (1ull << GROUP_SHIFT) - 1ull; // group of 16 CLs
    constexpr uint64_t GROUP_INDEX_SHIFT = VIRTUAL_ADDRESS_BITS - GROUP_SHIFT;
    constexpr uint64_t GROUP_INDEX_MASK = (1ull << GROUP_INDEX_SHIFT) - 1ull;
    constexpr uint64_t GROUP_POS_MASK = 15; // 1111b
    constexpr size_t GROUP_LEN_MAX = (1 << 25)-1;

    inline bool is_length_based(cl_group_t cg) {
        return cg & TYPE_MASK;
    }

    inline size_t get_length(cl_group_t cg) {
        return (cg & ~TYPE_MASK) >> GROUP_INDEX_SHIFT;
    }

    inline cl_group_index_t get_index(cl_group_t cg) {
        return cg & GROUP_INDEX_MASK;
    }

    inline uintptr_t get_ptr(cl_group_t cg) {
        return (cg & GROUP_INDEX_MASK) << GROUP_SHIFT;
    }

    inline uint64_t get_mask16(cl_group_t cg) {
        return cg >> GROUP_INDEX_SHIFT;
    }

    inline uint64_t get_mask64(cl_group_t cg) {
        uint8_t diff = cg & 2; //2 = 1 << log(64/16)
        uint8_t mask_shift = 16 * diff;
        return get_mask16(cg) << mask_shift;
    }
}

class LengthCLRange {
    uintptr_t ptr;
    size_t length;
public:
    class iterator {
        uintptr_t ptr;
        size_t length;
    public:
        inline iterator(uintptr_t p, size_t c): ptr(p), length(c) {}
        inline uintptr_t operator*() const {
            return ptr + (length << cl_group::GROUP_SHIFT);
        }

        inline iterator operator++() {
            length++;
            return *this;
        }

        inline bool operator!=(const iterator & other) const {
            return ptr != other.ptr || length != other.length;
        }
    };

    LengthCLRange(cl_group_t cg): ptr(cl_group::get_ptr(cg)), length(cl_group::get_length(cg)) {}

    iterator begin() {
        return iterator(ptr, 0);
    }

    iterator end() {
        return iterator(ptr, length);
    }
};

class MaskCLRange {
    uintptr_t ptr;
    uint64_t mask;
public:
    class iterator {
        uintptr_t ptr;
        uint64_t mask;
    public:
        inline iterator(uintptr_t p, uint64_t m): ptr(p), mask(m) {}
        inline uintptr_t operator*() const {
            int p = __builtin_ctzl(mask);
            return ptr + (p << CACHE_LINE_SHIFT);
        }

        inline iterator operator++() {
            uint64_t m = mask & -mask;
            mask ^= m;
            return *this; 
        }

        inline bool operator!=(const iterator & other) const {
            return ptr != other.ptr || mask != other.mask;
        }
    };

    MaskCLRange(cl_group_t cg): ptr(cl_group::get_ptr(cg)), mask(cl_group::get_mask16(cg)) {}
    MaskCLRange(uintptr_t p, uint64_t m): ptr(p), mask(m) {}

    iterator begin() {
        return iterator(ptr, mask);
    }

    iterator end() {
        return iterator(ptr, 0);
    }
};

template<typename F>
inline void process_cl_group(cl_group_t cg, F f) {
    if (cl_group::is_length_based(cg)) {
        for (auto cl_group_addr: LengthCLRange(cg))
            f(cl_group_addr, cl_group::FULL_MASK);
    } else
        f(cl_group::get_ptr(cg), cl_group::get_mask16(cg));
} // clgroup

} // RACoherence
#endif
