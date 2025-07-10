#ifndef _CACHE_AGENT_H_
#define _CACHE_AGENT_H_

#include "config.hpp"
#include "logBuffer.hpp"
#include "logger.hpp"
#include "memLayout.hpp"

extern std::atomic<bool> complete;

class CacheAgent {
    // local data
    unsigned count = 0;
    unsigned node_id;
    //CXL mem shared adta
    PerNode<LogBuffer> &bufs;
    //node local data
    CacheInfo &cache_info;

public:
    CacheAgent(unsigned nid, CXLPool &pool, NodeLocalMeta &node_meta): node_id(nid), bufs(pool.meta.bufs), cache_info(node_meta.cache_info) {}

    void run();
};

#endif
