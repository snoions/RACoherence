#include "cxlMalloc.hpp"

#ifdef HC_USE_DLMALLOC
mspace cxlhc_space;
#else
CXLHCPool *cxlhc_pool;
#endif
