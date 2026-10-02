#ifndef _CACHE_AGENT_H_
#define _CACHE_AGENT_H_

#include "config.hpp"
#include "logManager.hpp"
#include "logger.hpp"
#include "cacheInfo.hpp"

namespace RACoherence {

extern std::atomic<bool> complete;

class CacheAgent {
    unsigned count = 0;
    unsigned curr_node_id;

    // CXL mem shared adta
    LogManager *log_mgrs;
    // node local data
    CacheInfo &cache_info;

public:
    CacheAgent(CacheInfo &cinfo, LogManager *lmgrs, unsigned nid): curr_node_id(nid), log_mgrs(lmgrs), cache_info(cinfo) {}

    void run();
};

} // RACoherence

#endif
