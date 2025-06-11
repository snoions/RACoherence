#ifndef _COPIER_H_
#define _COPIER_H_

#include <unordered_set>
#include <iostream>

#include "unistd.h"

#include "config.hpp"
#include "logBuffer.hpp"

// TODO: use 1 thread per buffer, maybe with work stealing
// how to make stale_dir thread-safe?
extern thread_local unsigned node_id;

class CacheAgent {
    idx_t tails[NODECOUNT] = {0};
    std::unordered_set<uintptr_t> stale_dir;
    LogBuffer *buffers;

public:
    CacheAgent(LogBuffer *bufs): buffers(bufs) {}

    void run() {
        unsigned count = 0;
        while(count < EPOCH * WORKER_PER_NODE * (NODECOUNT-1)) {
            for (int i=0; i<NODECOUNT; i++) {
                if (i == node_id)
                    continue;
                Log* tail = buffers[i].consumeTail(tails[i]);
                if (!tail)
                    continue;
                for (auto invalid_cl: *tail) {
                    stale_dir.insert(invalid_cl);
                }
                std::cout << "node " << node_id << ": consume log " << count++ << " of " << i << std::endl;
            }
        }
    }
};

#endif
