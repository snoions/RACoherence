#include "cacheAgent.hpp"
#include "flushUtils.hpp"

namespace RACoherence {

void CacheAgent::run() {
    int idle_rounds = 0;
    while(!complete.load()) {
        // Every agent of a node scans every source queue: consumers claim logs, so several
        // threads can work on one queue at once and no static partition is needed.
        for (unsigned i = 0; i < NODE_COUNT; i++) {
            if (i == curr_node_id)
                continue;
            if (!log_mgrs[i].is_subscribed(curr_node_id))
                continue;

            // Small claims (AGENT_CLAIM_BATCH): see config.hpp.
            if (cache_info.consume_logs(log_mgrs, curr_node_id, i, AGENT_CLAIM_BATCH) == 0) {
                if (idle_rounds >= NODE_COUNT -1) {
                    cpu_pause();
                } else
                    idle_rounds ++;
                continue;
            }
            idle_rounds = 0;
        }
    }
    LOG_INFO("node " << curr_node_id << " cache agent done")
}

} // RACoherence
