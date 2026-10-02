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

static int find_nth_core_on_numa(int numa_id, int n) {
    if (numa_available() < 0) {
        LOG_ERROR("NUMA is not available on this system.")
        return -1;
    }

    // Get the bitmask of CPUs for the specified NUMA node
    struct bitmask* cpu_mask = numa_allocate_cpumask();
    if (numa_node_to_cpus(numa_id, cpu_mask) < 0) {
        LOG_ERROR("Failed to get CPUs for NUMA node " << numa_id)
        numa_free_cpumask(cpu_mask);
        return -1;
    }

    int current_count = 0;
    int target_cpu = -1;

    // Iterate through all possible CPU IDs in the bitmask size
    for (unsigned int i = 0; i <= cpu_mask->size; ++i) {
        if (numa_bitmask_isbitset(cpu_mask, i)) {
            if (current_count == n) {
                target_cpu = i;
                break;
            }
            current_count++;
        }
    }

    numa_free_cpumask(cpu_mask);
    return target_cpu; // Returns -1 if N exceeds available cores on this node
}

} // RACoherence

#endif
