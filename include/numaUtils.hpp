#ifndef _NUMA_UTIL_H_
#define _NUMA_UTIL_H_

#include <errno.h>
#include <numa.h>
#include <numaif.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>

#include "logger.hpp"

namespace RACoherence {

constexpr int LOCAL_NUMA_ID=0;
constexpr int REMOTE_NUMA_ID=1;

static int set_thread_affinity(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    int ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        LOG_ERROR("Error setting thread affinity: "<< strerror(ret))
        return ret;
    }
    return 0;
}

static int find_cpu_on_numa(unsigned &cpu_id, int target_numa_id) {
    for (int numa_id = -1; numa_id != target_numa_id; cpu_id++) {
        numa_id = numa_node_of_cpu(cpu_id);
        if (numa_id == -1){
            LOG_ERROR("Failed to NUMA node of cpu" << cpu_id << strerror(errno))
            return -1;
        }
    }
    return 0;
}

static void *remote_numa_alloc(size_t length) {
    void *map = mmap(NULL, length, PROT_READ|PROT_WRITE,
	MAP_PRIVATE|MAP_ANONYMOUS, 0, 0);
    if (map == NULL) {
        LOG_ERROR("shm creation failed: " << strerror(errno))
        return NULL;
    }

    unsigned long nodemask = 0;
    nodemask |= 1 << REMOTE_NUMA_ID;
    if(mbind(map, length, MPOL_BIND, &nodemask, sizeof(nodemask) * 8, 0) < 0) {
        LOG_ERROR("mbind buf failed: " << strerror(errno))
        return NULL;
    }

    memset(map, 0, length);
    return map;
}

static int run_on_local_numa() {
    if(numa_run_on_node(LOCAL_NUMA_ID)) {
        printf( "numa_run_on_node local failed: %s\n", strerror(errno) );
        return -1;
    }
    return 0;
}

} // RACoherence

#endif
