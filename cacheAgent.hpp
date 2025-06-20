#ifndef _COPIER_H_
#define _COPIER_H_

#include <unordered_set>
#include <iostream>

#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

extern thread_local unsigned node_id;

class CacheAgent {
    // local data
    unsigned count = 0;
    //CXL mem shared adta
    PerNode<LogBuffer> &bufs;
    //node local data
    CacheInfo &cache_info;

public:
    CacheAgent(PerNode<LogBuffer> &b, CacheInfo &cinfo): bufs(b), cache_info(cinfo) {}

    void run() {
        while(cache_info.consumed_count < EPOCH * WORKER_PER_NODE * (NODE_COUNT-1)) {
            for (unsigned i=0; i<NODE_COUNT; i++) {
                if (i == node_id)
                    continue;

                std::unique_lock<std::mutex> lk(bufs[i].getTailMutex(node_id), std::defer_lock);
                if (!lk.try_lock())
                    continue;

                Log* tail = bufs[i].takeTail(node_id);
                if (!tail)
                    continue;

                cache_info.process_log(tail);

                LOG_INFO("node " << node_id << " consume log " << cache_info.consumed_count << " of " << i);
                cache_info.consumed_count++;
                if (tail->is_release()) {
                    cache_info.update_clock(i);
                }
                tail->consume();
            }
        }
    }
};

#endif
