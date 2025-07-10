#ifndef _VECTOR_CLOCK_H_
#define _VECTOR_CLOCK_H_

#include <array>
#include <algorithm>
#include <cstddef>  // for std::size_t
#include <iostream>

#include "config.hpp"

//TODO: tree clocks could be faster for merges
class VectorClock {
public:
    using clock_t = unsigned;
    using sized_t = std::size_t;

private:
    std::array<clock_t, NODE_COUNT> vc{};

public:
    VectorClock() = default;
    VectorClock(const VectorClock&) = default;
    VectorClock(VectorClock&&) noexcept = default;
    VectorClock& operator=(const VectorClock&) = default;
    VectorClock& operator=(VectorClock&&) noexcept = default;

    // Increment the clock at a given index (local tick)
    void tick(sized_t index) {
        if (index < NODE_COUNT) {
            ++vc[index];
        }
    }

    // Merge this vector clock with another (element-wise max)
    void merge(const VectorClock& other) {
        for (sized_t i = 0; i < NODE_COUNT; ++i) {
            vc[i] = std::max(vc[i], other.vc[i]);
        }
    }

    // Happens-before comparison
    bool operator<(const VectorClock& other) const {
        bool strictly_less = false;
        for (sized_t i = 0; i < NODE_COUNT; ++i) {
            if (vc[i] > other.vc[i]) return false;
            if (vc[i] < other.vc[i]) strictly_less = true;
        }
        return strictly_less;
    }

    // Happens-before or concurrent comparison
    bool operator<=(const VectorClock& other) const {
        for (sized_t i = 0; i < NODE_COUNT; ++i) {
            if (vc[i] > other.vc[i]) return false;
        }
        return true;
    }
    
    clock_t& operator[](sized_t index) {
        return vc[index];
    }

    const clock_t& operator[](sized_t index) const {
        return vc[index];
    }

    // For debugging/printing
    friend std::ostream& operator<<(std::ostream& os, const VectorClock& v) {
        os << "[";
        for (sized_t i = 0; i < NODE_COUNT; ++i) {
            os << v.vc[i];
            if (i != NODE_COUNT - 1) os << ", ";
        }
        os << "]";
        return os;
    }

    // Optional: Expose for debugging or testing
    const std::array<clock_t, NODE_COUNT>& get() const { return vc; }
};

#endif
