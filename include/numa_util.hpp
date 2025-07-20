#ifndef _NUMA_UTIL_H_
#define _NUMA_UTIL_H_

#include <errno.h>
#include <numa.h>
#include <numaif.h>
#include <stdio.h>
#include <sys/mman.h>
constexpr int LOCAL_NUMA_ID=0;
constexpr int REMOTE_NUMA_ID=1;

static void *remote_numa_alloc(size_t length) {
    void *map = mmap(NULL, length, PROT_READ|PROT_WRITE,
	MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    if (map == NULL) {
        printf( "shm creation failed: %s\n", strerror(errno) );
        exit(1);
    } 

    unsigned long nodemask = 0;
    nodemask |= 1 << REMOTE_NUMA_ID;
    if(mbind(map, length, MPOL_BIND, &nodemask, sizeof(nodemask) * 8, 0) < 0) {
        printf( "mbind buf failed: %s\n", strerror(errno) );
        exit(1);
    }

    memset(map, 0, length);
    return map;
}

static void run_on_local_numa() {
    if(numa_run_on_node(LOCAL_NUMA_ID)) {
        printf( "numa_run_on_node local failed: %s\n", strerror(errno) );
        exit(1);
    }
}

#endif
