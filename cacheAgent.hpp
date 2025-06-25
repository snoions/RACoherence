#ifndef _COPIER_H_
#define _COPIER_H_

#include <unordered_set>
#include <iostream>

#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

extern thread_local unsigned node_id;
extern std::atomic<bool> complete;

class CacheAgent {
    // local data
    unsigned count = 0;
    //CXL mem shared adta
    PerNode<LogBuffer> &bufs;
    //node local data
    CacheInfo &cache_info;

public:
    CacheAgent(CXLPool &pool, NodeLocalMeta &node_meta): bufs(pool.meta.bufs), cache_info(node_meta.cache_info) {}

    void run() {
        while(!complete.load()) {
            for (unsigned i=0; i<NODE_COUNT; i++) {
                if (i == node_id)
                    continue;

#ifdef USER_CONSUME_LOGS
                std::unique_lock<std::mutex> lk(bufs[i].get_tail_mutex(node_id), std::defer_lock);
                if (!lk.try_lock())
                    continue;
#endif

                Log* tail = bufs[i].try_take_tail(node_id);
                if (!tail)
                    continue;

                cache_info.process_log(tail);

                LOG_INFO("node " << node_id << " consume log " << ++cache_info.consumed_count << " from " << i);
                if (tail->is_release()) {
                    cache_info.update_clock(i);
                }
                tail->consume();
            }
        }
    }
};

#endif
