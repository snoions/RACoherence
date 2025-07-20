#ifndef _MASKED_PTR_H
#define _MASKED_PTR_H

#include "config.hpp"

using masked_ptr_t = uint64_t;
namespace masked_ptr {
    constexpr int GROUP_SHIFT = CACHE_LINE_SHIFT + 4; // group of 16 CLs
    constexpr uint64_t PTR_SHIFT = VIRTUAL_ADDRESS_BITS - GROUP_SHIFT;
    constexpr uint64_t PTR_MASK = (1ull << PTR_SHIFT) - 1ull;
    // index within group
    constexpr int INDEX_MASK = 15; // 1111b
}

using namespace masked_ptr;

static inline uintptr_t get_ptr(masked_ptr_t pm) {
    return (pm  & PTR_MASK) << GROUP_SHIFT;
}

static inline uint64_t get_mask16(masked_ptr_t pm) {
    return pm >> PTR_SHIFT;
}

static inline uint64_t get_mask64(masked_ptr_t pm) {
    uint8_t diff = pm & 2; //2 = 1 << log(64/16)
    uint8_t mask_shift = 16 * diff;
    return get_mask16(pm) << mask_shift;
}

class MaskedPtrRange {
    uintptr_t ptr;
    uint64_t mask;
public:
    class iterator {
        uintptr_t ptr;
        uint64_t mask;
    public:
        inline iterator(uintptr_t p, uint64_t m): ptr(p), mask(m) {}
        inline const uintptr_t operator*() const {
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

    MaskedPtrRange(masked_ptr_t pm): ptr(get_ptr(pm)), mask(get_mask16(pm)) {}

    iterator begin() {
        return iterator(ptr, mask);
    }

    iterator end() {
        return iterator(ptr, 0);
    }
};

#endif
