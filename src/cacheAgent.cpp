#include "cacheAgent.hpp"
#include "flushUtils.hpp"

namespace RACoherence {

void CacheAgent::run() {
    int idle_rounds = 0;
    while(!complete.load()) {
        for (unsigned i=0; i<NODE_COUNT; i++) {
            if (i == node_id)
                continue;
            if (!log_mgrs[i].is_subscribed(node_id))
                continue;

#if CONSUME_HELPING || CONSUME_HELPING_IN_LOCK
            auto &mtx = log_mgrs[i].get_head_mutex(node_id);
            if (!mtx.try_lock())
                continue;
#endif
            clock_t clk = 0;
            for (unsigned j=0; j<LOG_MAX_BATCH; j++) {
                const PubEntry* entry = log_mgrs[i].take_head(node_id);
                if (!entry) {
                    if (idle_rounds >= NODE_COUNT -1) {
                        cpu_pause();
                    } else
                        idle_rounds ++;
                    break;
                }
                Log* log = entry->log.load(std::memory_order_relaxed);

                if (entry->is_rel)
                    clk = entry->idx.load(std::memory_order_relaxed);
                idle_rounds = 0;
                cache_info.process_log(*log);

                STATS(cache_info.consumed_count[i]++)
                LOG_DEBUG("node " << node_id << " consume log " << cache_info.consumed_count[i] << " from " << i << " clock=" << cache_info.get_clock(i))
                log_mgrs[i].consume_head(node_id);
            }
            if (clk) {
                // mutex unlock takes care of invalidate fence for CONSUME_HELPING
#if EAGER_INVALIDATE && ! (CONSUME_HELPING || CONSUME_HELPING_IN_LOCK)
                invalidate_fence();
#endif
                cache_info.update_clock(i, clk);
            }
#if CONSUME_HELPING || CONSUME_HELPING_IN_LOCK
            mtx.unlock();
#endif
        }
    }
    LOG_INFO("node " << node_id << " cache agent done")
}

} // RACoherence
