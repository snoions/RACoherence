
#include "config.hpp"
#include "cacheAgent.hpp"
#include "cacheInfo.hpp"
#include "logger.hpp"
#include "microbench.hpp"
#include "numaUtils.hpp"
#include "runtime.hpp"

using namespace RACoherence;

constexpr unsigned WORKER_PER_NODE = 4;

struct CXLRoot {
    CXLPool *cxl_pool;
};

CXLRoot *root;
WORKLOAD_TYPE workload;

void *run_microbench(void* /*arg*/) {
    Microbench(*root->cxl_pool, workload).run();
    return nullptr;
}

int main() {
    const char* server_idx_ch = std::getenv("RAC_SERVER_IDX");
    char* endptr;
    long server_idx = strtol(server_idx_ch, &endptr, 10);
    if (endptr == server_idx_ch || server_idx < 0 || server_idx > NODE_COUNT) {
        std::cerr << "Invalid RAC_SERVER_IDX: " << server_idx_ch << std::endl;
        return 1;
    }

    rac_init(server_idx, CXL_HC_RANGE, CXL_NHC_RANGE, sizeof(CXLRoot));
    root = static_cast<CXLRoot*>(rac_get_user_root());
    if (server_idx == 0) {
        root->cxl_pool = new(cxlnhc_malloc(sizeof(CXLPool))) CXLPool();
    } 
    rac_get_root_barrier()->wait();

    pthread_t microbench_group[WORKER_PER_NODE];
    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned i=0; i<WORKER_PER_NODE; i++) {
       int ret = rac_thread_create(server_idx, &microbench_group[i], run_microbench, nullptr);
       assert(!ret);
    }

    for (unsigned i=0; i<WORKER_PER_NODE; i++) {
       int ret = rac_thread_join(server_idx, microbench_group[i], nullptr);
       assert(!ret);
    }

    rac_get_root_barrier()->wait();
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end - start;

    if(server_idx == 0) {
        std::cout << "Elapsed time: " << elapsed.count() / 1000 << "s" << std::endl;
        root->cxl_pool->~CXLPool();
    }
    rac_shutdown();
    return 0;
}
