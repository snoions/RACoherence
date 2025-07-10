#ifndef _DIRTY_CL_TRACKER_
#define _DIRTY_CL_TRACKER_

#include <unordered_set>

#include "absl/container/flat_hash_set.h"
#include "config.hpp"
#include "logBuffer.hpp"

struct CLHash {
    inline std::size_t operator()(const virt_addr_t& a) const
    {
        return (a >> CACHE_LINE_SHIFT) & (LOG_SIZE-1);
    }
};

class LocalCLTracker {
    using Table = std::unordered_set<virt_addr_t, CLHash>;
    //using Table = absl::flat_hash_set<virt_addr_t, CLHash>;
    using iterator = Table::iterator;
    using const_iterator = Table::const_iterator;

    Table _ht;

public:
    LocalCLTracker(): _ht(LOG_SIZE) {}

    std::pair<iterator, bool> insert(virt_addr_t cl_addr) {
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
