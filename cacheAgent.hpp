#ifndef _COPIER_H_
#define _COPIER_H_

#include <unordered_set>
#include <iostream>

#include "unistd.h"

#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

// TODO: stale_dir needs to be concurrent
extern thread_local unsigned node_id;

class CacheAgent {
    //user local data
    unsigned count = 0;
    //CXL mem shared adta
    PerNodeData<LogBuffer> *bufs;
    //node local data
    CacheInfo *cache_info;
    std::unique_ptr<CacheInfo::TaskQueue> local_tq; 

public:
    CacheAgent(std::array<LogBuffer, NODE_COUNT> *b, CacheInfo *cinfo): bufs(b), cache_info(cinfo), local_tq(std::make_unique<CacheInfo::TaskQueue>()) {}

    void process_log(Log *log) {
        for (auto invalid_cl: *log) {
            cache_info->tracker.mark_dirty(invalid_cl);
        }
    }

    void process_tasks() {
        if (!cache_info->task_queue.get([](auto &self) {return self->empty();})) {
            cache_info->task_queue.mod([&](auto &self) {
                    local_tq.swap(self);
                    *self = {};
                });
        }
        //no data race as the tick is in run() 
        const auto &clock = cache_info->clock.get_raw();
        for(auto t = local_tq->front(); local_tq->empty(); local_tq->pop()) {
            auto [idx, val] = t;
            while(clock[idx] < val) {
                Log* tail = (*bufs)[idx].consumeTail(node_id);
                assert(tail);
                process_log(tail);
                if (tail->is_release())
                    cache_info->clock.mod([=](auto &self) { self.tick(idx); });
                LOG_INFO("node " << node_id << " consume log " << count++ << " of " << idx);
            }    
        }
    }

    void run() {
        while(count < EPOCH * WORKER_PER_NODE * (NODE_COUNT-1)) {
            for (unsigned i=0; i<NODE_COUNT; i++) {
                if (i == node_id)
                    continue;
                Log* tail = (*bufs)[i].consumeTail(node_id);
                if (!tail)
                    continue;

                process_log(tail);

                LOG_INFO("node " << node_id << " consume log " << count++ << " of " << i); 
                if (tail->is_release()) {
                    cache_info->clock.mod([&](auto &self) {
                        self.tick(i);
                    });
                    //process_tasks();
                }
            }
        }
    }
};

#endif
