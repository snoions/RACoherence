#ifndef _CACHE_AGENT_H_
#define _CACHE_AGENT_H_

#include "config.hpp"
#include "logManager.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

extern std::atomic<bool> complete;
extern thread_local unsigned node_id;

class CacheAgent {
    // local data
    unsigned count = 0;
    //CXL mem shared adta
    PerNode<LogManager> &bufs;
    //node local data
    CacheInfo &cache_info;

public:
    CacheAgent(CXLPool &pool, NodeLocalMeta &node_meta): bufs(pool.meta.bufs), cache_info(node_meta.cache_info) {}

    void run();
};

#endif
