#include <thread>

#include "config.hpp"
#include "cacheAgent.hpp"
#include "cacheInfo.hpp"
#include "logger.hpp"
#include "microbench.hpp"
#include "numaUtils.hpp"
#include "runtime.hpp"

int main() {
    rac_init(0);
    CXLPool *cxl_pool = new (cxlnhc_malloc(sizeof(CXLPool))) CXLPool(); // new (cxl_nhc_buf) CXLPool();

    int worker_count = NODE_COUNT * WORKER_PER_NODE;
    std::thread microbench_group[worker_count];
    RACThreadArgs arg_data[worker_count];
    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned i=0; i<NODE_COUNT; i++) {
        std::function<void *(void*)> run_microbench = [=] (void*) {
            Microbench(*cxl_pool, i).run();
            return nullptr;
        };
        for (int j=0; j<WORKER_PER_NODE;j++) {
            int index = i * WORKER_PER_NODE + j;
            arg_data[index] = {i, run_microbench, nullptr};
            microbench_group[index] = std::thread(rac_thread_func_wrapper, &arg_data[index]);
        }
    }

    for (unsigned i=0; i < worker_count ; i++)
        microbench_group[i].join();

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;
    std::cout << "Elapsed time: " << elapsed.count() / 1000 << "s" << std::endl;

    cxl_pool->~CXLPool();
    rac_shutdown();
    return 0;
}

