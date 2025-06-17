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
    unsigned count = 0;
    std::unordered_set<uintptr_t> stale_dir;
    PerNodeData<LogBuffer> *bufs;
    CacheInfo *cache_info;
    std::unique_ptr<CacheInfo::TaskQueue> local_tq; 

public:
    CacheAgent(std::array<LogBuffer, NODECOUNT> *b, CacheInfo *cinfo): bufs(b), cache_info(cinfo), local_tq(std::make_unique<CacheInfo::TaskQueue>()) {}

    void process_log(Log *log) {
        for (auto invalid_cl: *log) {
            stale_dir.insert(invalid_cl);
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
                LOG_DEBUG("node " << node_id << " consume log " << count++ << " of " << idx);
            }    
        }
    }

    void run() {
        while(count < EPOCH * WORKER_PER_NODE * (NODECOUNT-1)) {
            for (unsigned i=0; i<NODECOUNT; i++) {
                if (i == node_id)
                    continue;
                Log* tail = (*bufs)[i].consumeTail(node_id);
                if (!tail)
                    continue;

                process_log(tail);

                if (tail->is_release())
                    cache_info->clock.mod([&](auto &self) {
                        self.tick(i);
                    });
                LOG_DEBUG("node " << node_id << " consume log " << count++ << " of " << i); 

               process_tasks();
            }
        }
    }
};

#endif
