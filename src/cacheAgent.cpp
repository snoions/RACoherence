#include "cacheAgent.hpp"
#include "flushUtils.hpp"

namespace RACoherence {

void CacheAgent::run() {
    int idle_rounds = 0;
    while(!complete.load()) {
        for (unsigned i=0; i<NODE_COUNT; i=(i+1==node_id)? i+2: i+1) {
            if (!log_mgrs[i].is_subscribed(node_id))
                continue;

#ifdef USER_HELP_CONSUME
            CLHMutex &mtx = log_mgrs[i].get_head_mutex(node_id);
            if (!mtx.try_lock())
                continue;
#endif
            clock_t clk = 0;
            for (unsigned j=0; j<LOG_MAX_BATCH; j++) {
                Log* log = log_mgrs[i].take_head(node_id);
                if (!log) {
                    if (idle_rounds >= NODE_COUNT -1) {
                        cpu_pause();
                    } else
                        idle_rounds ++;
                    break;
                }

                idle_rounds = 0;
                cache_info.process_log(*log);

                if (log->is_release()) {
//#if EAGER_INVALIDATE
//                    invalidate_fence();
//#endif
//                    auto clk = log->get_log_idx();
//                    cache_info.update_clock(i, clk);
                    clk = log->get_log_idx();
                }
                STATS(cache_info.consumed_count[i]++)
                LOG_DEBUG("node " << node_id << " consume log " << cache_info.consumed_count[i] << " from " << i << " clock=" << cache_info.get_clock(i))
                log_mgrs[i].consume_head(node_id);
            }
            if (clk) {
                // mutex unlock takes care of invalidate fence for USER_HELP_CONSUME
#if EAGER_INVALIDATE && !defined(USER_HELP_CONSUME)
                invalidate_fence();
#endif
                cache_info.update_clock(i, clk);
            }
#ifdef USER_HELP_CONSUME
            mtx.unlock();
#endif
        }
    }
    LOG_INFO("node " << node_id << " cache agent done")
}

} // RACoherence
