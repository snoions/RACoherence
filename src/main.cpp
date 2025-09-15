
#include "config.hpp"
#include "cacheAgent.hpp"
#include "cacheInfo.hpp"
#include "logger.hpp"
#include "microbench.hpp"
#include "numaUtils.hpp"
#include "runtime.hpp"

CXLPool *cxl_pool;

void *run_microbench(void *) {
    Microbench(*cxl_pool).run();
    return nullptr;
}

int main() {
    rac_init(0, CXL_HC_RANGE, CXL_NHC_RANGE);
    cxl_pool = new (cxlnhc_malloc(sizeof(CXLPool))) CXLPool(); // new (cxl_nhc_buf) CXLPool();

    int worker_count = NODE_COUNT * WORKER_PER_NODE;
    pthread_t microbench_group[worker_count];

    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned i=0; i<NODE_COUNT; i++) {
        for (int j=0; j<WORKER_PER_NODE;j++) {
            int index = i * WORKER_PER_NODE + j;
            int ret = rac_thread_create(i, &microbench_group[index], run_microbench, nullptr);
            assert(!ret);
        }
    }

    for (unsigned i=0; i<NODE_COUNT; i++) {
        for (int j=0; j<WORKER_PER_NODE;j++) {
            int index = i * WORKER_PER_NODE + j;
            int ret = rac_thread_join(i, microbench_group[index], nullptr);
            assert(!ret);
        }
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() / 1000 << "s" << std::endl;

    cxl_pool->~CXLPool();
    rac_shutdown();
    return 0;
}

