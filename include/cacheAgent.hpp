#ifndef _CACHE_AGENT_H_
#define _CACHE_AGENT_H_

#include "config.hpp"
#include "logManager.hpp"
#include "logger.hpp"
#include "cacheInfo.hpp"

extern std::atomic<bool> complete;
constexpr size_t LOG_MAX_BATCH = 100;

class CacheAgent {
    unsigned count = 0;
    unsigned node_id;

    // CXL mem shared adta
    LogManager *log_mgrs;
    // node local data
    CacheInfo &cache_info;

public:
    CacheAgent(CacheInfo &cinfo, LogManager *lmgrs, unsigned nid): cache_info(cinfo), log_mgrs(lmgrs), node_id(nid) {}

    void run();
};

#endif
