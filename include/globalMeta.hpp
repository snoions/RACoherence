#ifndef _GLOBAL_META_H_
#define _GLOBAL_META_H_

#include <atomic>

#include "cxlSync.hpp"
#include "extentPool.hpp"
#include "logManager.hpp"
#include "slabPool.hpp" 

namespace RACoherence {

#ifdef HC_USE_CUSTOM_POOL
// 16-bit tag
// only valid for 65,536 CAS operations
using CXLHCPool = SlabPool<8, 128>;
#else
using CXLHCPool = ExtentPool;
#endif

struct GlobalMeta {
    std::atomic<bool> started;
    std::atomic<unsigned> curr_tid;
    LogManager log_mgrs[NODE_COUNT];
    void *user_root;
    CXLBarrier root_barrier;
    CXLHCPool cxlhc_pool;
    ExtentPool cxlnhc_pool;
};

}
#endif
