#ifndef _CONFIG_H_
#define _CONFIG_H_

#define CACHELINESIZE_LOG 6
#define CACHELINESIZE (1 << CACHELINESIZE_LOG)
#define CACHELINEMASK (uintptr_t) (~(CACHELINESIZE-1))
#define NODECOUNT 4
#define WORKER_PER_NODE 10
#define EPOCH 100
#define LOGSIZE (1 << 10)
#define LOGBUFSIZE (1 << 10)
#define CXLMEM_RANGE (1 << 4)
#define CXLMEM_ATOMIC_RANGE (1 << 2)

#endif
