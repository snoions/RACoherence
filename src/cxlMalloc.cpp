#include "cxlMalloc.hpp"

#ifdef HC_USE_DLMALLOC
mspace cxlhc_space;
#else
CXLHCPool *cxlhc_pool;
#endif
ExtentPool *cxlnhc_extent_pool;
extent_hooks_t cxlnhc_hooks = {
    .alloc = cxlnhc_extent_alloc,
    .dalloc = cxlnhc_extent_dalloc,
    .commit = nullptr,
    .decommit = nullptr,
    .purge_lazy = nullptr,
    .purge_forced = nullptr,
    .split = nullptr,
    .merge = nullptr
};
