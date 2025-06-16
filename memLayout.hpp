#ifndef _MEM_LAYOUT_H_
#define _MEM_LAYOUT_H_

#include <map>

#include "logBuffer.hpp"
#include "util.hpp"
    
static bool isAtomic(uintptr_t addr) {
    return addr < CXLMEM_ATOMIC_RANGE;
}

struct ALocMeta {
    Log* log;
    VectorClock clock;
};

// emulates allocated atomic locations
using ALocMap = std::map<uintptr_t, Monitor<ALocMeta>>;

struct CXLMemMeta {
    PerNodeData<LogBuffer> buffers;
    PerNodeData<Monitor<VectorClock>> cache_clocks;
    //TODO: support dynamically allocated atomic locs
    ALocMap alocs;

    CXLMemMeta () {
        for (int i=0; i<CXLMEM_ATOMIC_RANGE; i++)
            alocs[i];
    }
};

#endif
