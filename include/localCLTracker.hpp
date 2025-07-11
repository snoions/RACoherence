#ifndef _DIRTY_CL_TRACKER_
#define _DIRTY_CL_TRACKER_

#include <unordered_set>

#include "config.hpp"

struct CLHash {
    inline std::size_t operator()(const uintptr_t& a) const
    {
        return (a >> CACHE_LINE_SHIFT);
    }
};

class LocalCLTracker {
    using Table = std::unordered_set<uintptr_t, CLHash>;
    //using Table = std::set<uintptr_t>;
    //using Table = absl::flat_hash_set<uintptr_t, CLHash>;
    using iterator = Table::iterator;
    using const_iterator = Table::const_iterator;

    Table _ht;

public:

    std::pair<iterator, bool> insert(uintptr_t cl_addr) {
        return _ht.insert(cl_addr);
    }

    iterator begin() {
        return _ht.begin();
    }

    iterator end() {
        return _ht.end();
    }

    const_iterator begin() const {
        return _ht.begin();
    }

    const_iterator end() const {
        return _ht.end();
    }

    void clear() {
        _ht.clear();
    }

    size_t size() {
        return _ht.size();
    }
};

#endif
