#ifndef _GLOBAL_META_H_
#define _GLOBAL_META_H_

#include <atomic>

#include "cxlMalloc.hpp"
#include "cxlSync.hpp"
#include "logManager.hpp"

namespace RACoherence {
struct GlobalMeta {
    std::atomic<bool> started;
    std::atomic<unsigned> curr_tid;
    LogManager log_mgrs[NODE_COUNT];
    void *user_root;
    CXLBarrier root_barrier;
    AllocMeta alloc_meta;
};

}
#endif
