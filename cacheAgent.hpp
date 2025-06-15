#ifndef _COPIER_H_
#define _COPIER_H_

#include <unordered_set>
#include <iostream>

#include "unistd.h"

#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

// TODO: stale_dir probably needs to be concurrent. Single writer or multiple writer?
extern thread_local unsigned node_id;

class CacheAgent {
    std::unordered_set<uintptr_t> stale_dir;
    PerNodeData<LogBuffer> *bufs;
    Monitor<VectorClock> *cache_clock;

public:
    CacheAgent(std::array<LogBuffer, NODECOUNT> *b, Monitor<VectorClock> *clk): bufs(b), cache_clock(clk) {}

    void run() {
        unsigned count = 0;
        while(count < EPOCH * WORKER_PER_NODE * (NODECOUNT-1)) {
            for (int i=0; i<NODECOUNT; i++) {
                if (i == node_id)
                    continue;
                Log* tail = (*bufs)[i].consumeTail(node_id);
                if (!tail)
                    continue;
                for (auto invalid_cl: *tail) {
                    stale_dir.insert(invalid_cl);
                }
                cache_clock->mod([&](auto &cl) {
                    cl.tick(i);
                });
                LOG_DEBUG("node " << node_id << " consume log " << count++ << " of " << i);
            }
        }
    }
};

#endif
