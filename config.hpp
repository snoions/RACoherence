#ifndef _CONFIG_H_
#define _CONFIG_H_

#define CACHELINESIZE_LOG 6
#define CACHELINESIZE (1 << CACHELINESIZE_LOG)
#define CACHELINEMASK (uintptr_t) (~(CACHELINESIZE-1))
#define NODECOUNT 2
#define EPOCH 100

#endif
