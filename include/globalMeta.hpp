#ifndef _GLOBAL_META_H_
#define _GLOBAL_META_H_

#include <atomic>

#include "cxlMalloc.hpp"
#include "cxlSync.hpp"
#include "logManager.hpp"

namespace RACoherence {
struct GlobalHCMeta {
    std::atomic<bool> started;
    std::atomic<unsigned> curr_tid;
    void *user_root;
    CXLBarrier root_barrier;
    AllocMeta alloc_meta;
};

struct GlobalNHCMeta {
    LogManager log_mgrs[NODE_COUNT];
};

}
#endif
