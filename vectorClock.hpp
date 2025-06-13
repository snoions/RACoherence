#ifndef _VECTOR_CLOCK_H_
#define _VECTOR_CLOCK_H_

#include <array>
#include <algorithm>
#include <cstddef>  // for std::size_t
#include <iostream>

#include "config.hpp"

class VectorClock {
public:
    using clock_t = unsigned;
    using sized_t = std::size_t;

private:
    std::array<clock_t, NODECOUNT> vc{};

public:
    // Increment the clock at a given index (local tick)
    void tick(sized_t index) {
        if (index < NODECOUNT) {
            ++vc[index];
        }
    }

    // Merge this vector clock with another (element-wise max)
    void merge(const VectorClock& other) {
        for (sized_t i = 0; i < NODECOUNT; ++i) {
            vc[i] = std::max(vc[i], other.vc[i]);
        }
    }

    // Happens-before comparison
    bool operator<(const VectorClock& other) const {
        bool strictly_less = false;
        for (sized_t i = 0; i < NODECOUNT; ++i) {
            if (vc[i] > other.vc[i]) return false;
            if (vc[i] < other.vc[i]) strictly_less = true;
        }
        return strictly_less;
    }

    // Happens-before or concurrent comparison
    bool operator<=(const VectorClock& other) const {
        for (sized_t i = 0; i < NODECOUNT; ++i) {
            if (vc[i] > other.vc[i]) return false;
        }
        return true;
    }

    // Happens-before or concurrent comparison at a certain node
    bool le_at(const VectorClock& other, unsigned nid) const {
        for (sized_t i = 0; i < NODECOUNT; ++i) {
            if (i==nid)
                continue;
            if (vc[i] > other.vc[i]) return false;
        }
        return true;
    }
        // For debugging/printing
    friend std::ostream& operator<<(std::ostream& os, const VectorClock& v) {
        os << "[";
        for (sized_t i = 0; i < NODECOUNT; ++i) {
            os << v.vc[i];
            if (i != NODECOUNT - 1) os << ", ";
        }
        os << "]";
        return os;
    }

    // Optional: Expose for debugging or testing
    const std::array<clock_t, NODECOUNT>& get() const { return vc; }
};

#endif
