#ifndef _MEM_LAYOUT_H_
#define _MEM_LAYOUT_H_

#include <map>
#include <queue>

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
    //TODO: support dynamically allocated atomic locs
    ALocMap alocs;

    CXLMemMeta () {
        for (int i=0; i<CXLMEM_ATOMIC_RANGE; i++)
            alocs[i];
    }
};


struct CacheInfo {
    using Task = std::pair<VectorClock::sized_t, VectorClock::clock_t>;
    using TaskQueue = std::queue<Task>;

    Monitor<VectorClock> clock;
    Monitor<std::unique_ptr<TaskQueue>> task_queue;

    CacheInfo(): task_queue(Monitor(std::make_unique<TaskQueue>())) {}
};

struct NodeLocalMeta{
    CacheInfo cache_info;
};

#endif
