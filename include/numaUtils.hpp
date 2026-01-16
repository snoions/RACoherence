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

static int pin_to_core(int core_id) {
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
            LOG_ERROR("Failed to find NUMA node of cpu" << cpu_id << strerror(errno))
            return -1;
        }
    }
    return 0;
}

} // RACoherence

#endif
