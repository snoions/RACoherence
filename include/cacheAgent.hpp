#ifndef _CACHE_AGENT_H_
#define _CACHE_AGENT_H_

#include "config.hpp"
#include "logManager.hpp"
#include "logger.hpp"
#include "cacheInfo.hpp"

namespace RACoherence {

extern std::atomic<bool> complete;
constexpr size_t LOG_MAX_BATCH = 100;

class CacheAgent {
    unsigned count = 0;
    unsigned curr_node_id;
    unsigned target_node_begin;
    unsigned target_node_end;

    // CXL mem shared adta
    LogManager *log_mgrs;
    // node local data
    CacheInfo &cache_info;

public:
    CacheAgent(CacheInfo &cinfo, LogManager *lmgrs, unsigned nid, unsigned target_begin, unsigned target_end): cache_info(cinfo), log_mgrs(lmgrs), curr_node_id(nid), target_node_begin(target_begin), target_node_end(target_end) {}

    void run();
};

} // RACoherence

#endif
