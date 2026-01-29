#include "memoryPool.hpp"
#include "jemalloc/jemalloc.h"
#include "extentPool.hpp"
#include <gtest/gtest.h>

using namespace RACoherence;

TEST(MemoryPoolTest, Allocs)
{
    constexpr size_t POOL_SIZE = 1024 * 1024;
    static char buffer[POOL_SIZE];

    MemoryPool<32, 64, 128> pool(buffer, POOL_SIZE);

    void* p1 = pool.allocate(20);   // 32B pool
    void* p2 = pool.allocate(60);   // 64B pool
    void* p3 = pool.allocate(100);  // 128B pool

    std::cout << "Allocated: " << p1 << " " << p2 << " " << p3 << "\n";

    pool.deallocate(p1, 20);
    pool.deallocate(p2, 60);
    pool.deallocate(p3, 100);
    std::cout << "Deallocated: " << p1 << " " << p2 << " " << p3 << "\n";
}

TEST(ExtentPoolTest, Allocs)
{
    constexpr size_t POOL_SIZE = 1024 * 1024;
    static char buffer[POOL_SIZE];

    ExtentPool pool(buffer, POOL_SIZE);

    void* p1 = pool.allocate(20, 16);   // 32B pool
    void* p2 = pool.allocate(60, 32);   // 64B pool
    void* p3 = pool.allocate(100, 64);  // 128B pool

    std::cout << "Allocated: " << p1 << " " << p2 << " " << p3 << "\n";

    pool.deallocate(p1, 20);
    pool.deallocate(p2, 60);
    pool.deallocate(p3, 100);
    std::cout << "Deallocated: " << p1 << " " << p2 << " " << p3 << "\n";
}


static ExtentPool* g_extent_pool;

inline void* my_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    std::cout << "[hook] allocate size=" << size << "\n";
    if (!g_extent_pool) return nullptr;

    void* p = g_extent_pool->allocate(size, alignment);
    if (!p) {
        return nullptr;
    }

    if (zero && *zero) {
        std::memset(p, 0, size);
        *zero = false; // we satisfied zero
    }
    if (commit) *commit = true;

    return p;
}

inline bool my_extent_dalloc(extent_hooks_t* /*hooks*/,
                             void* addr, size_t size, bool /*committed*/, unsigned /*arena_ind*/) {
    std::cout << "[hook] extent_dalloc addr=" << addr << " size=" << size << "\n";
    if (!g_extent_pool) return true; // claim we handled it
    g_extent_pool->deallocate(addr, size);
    return true;
}

static extent_hooks_t my_hooks = {
    .alloc = my_extent_alloc,
    .dalloc = my_extent_dalloc,
    .commit = nullptr,
    .decommit = nullptr,
    .purge_lazy = nullptr,
    .purge_forced = nullptr,
    .split = nullptr,
    .merge = nullptr
};

constexpr size_t POOL_SIZE = 1 << 30;
alignas(4096) static char buffer[POOL_SIZE];

TEST(JemallocPoolTest, Allocs) {
    ExtentPool pool(buffer, POOL_SIZE);
    g_extent_pool = &pool;

    int ret;
    // Assign hooks
    extent_hooks_t* new_hooks = &my_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    if ((ret = je_mallctl("arena.0.extent_hooks", &old_hooks, &olen, &new_hooks, sizeof(new_hooks))))
        std::cerr << "mallctl arena.extent_hooks returned " << strerror(ret) << std::endl;
    je_mallctl("thread.tcache.flush", NULL, NULL, NULL, 0);

    void* p1 = je_mallocx(32, 0);
    void* p2 = je_mallocx(100, 0);
    void* p3 = je_mallocx(200, 0);
    std::cout << "Allocated p1=" << p1 << ", p2=" << p2 << ", p3=" << p3 << "\n";

    je_dallocx(p1, 0);
    je_dallocx(p2, 0);
    je_dallocx(p3, 0);
    std::cout << "Freed p1, p2, p3\n";
}
