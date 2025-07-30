#include "cacheAgent.hpp"

void CacheAgent::run() {
    while(!complete.load()) {
        for (unsigned i=0; i<NODE_COUNT; i++) {
            if (i == node_id)
                continue;

#ifdef USER_HELP_CONSUME
            std::unique_lock<std::mutex> lk(bufs[i].get_head_mutex(node_id), std::defer_lock);
            if (!lk.try_lock())
                continue;
#endif

            for (unsigned j=0; j<LOG_MAX_BATCH; j++) {
                Log* log = bufs[i].take_head(i, node_id);
                if (!log)
                    break;

                cache_info.process_log(*log);

                if (log->is_release()) {
                    cache_info.update_clock(i);
                }
                LOG_INFO("node " << node_id << " consume log " << ++cache_info.consumed_count[i] << " from " << i << " clock=" << cache_info.get_clock(i))
                bufs[i].consume_head(node_id);
                if(log->is_release())
                    break;
            }
        }
    }
}

