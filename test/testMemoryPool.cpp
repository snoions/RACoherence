#include "memoryPool.hpp"
#include "jemallocPool.hpp"
#include <gtest/gtest.h>

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

static ExtentPool* g_extent_pool;

inline void* my_extent_alloc(extent_hooks_t* /*hooks*/,
                             void* /*new_addr*/, size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned /*arena_ind*/) {
    std::cout << "[hook] extent_alloc requested size=" << size << " align=" << alignment << "\n";
    if (!g_extent_pool) return nullptr;

    void* p = g_extent_pool->alloc_extent(size, alignment);
    if (!p) {
        std::cout << "[hook] alloc_extent failed (out of pool)\n";
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
    g_extent_pool->dealloc_extent(addr, size);
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

    // Create new arena
    //size_t sz = sizeof(my_arena_index);
    //if ((ret = mallctl("arenas.create", &my_arena_index, &sz, nullptr, 0)))
    //    LOG_ERROR("mallctl arena.create returned " << strerror(ret))
    // Assign hooks
    extent_hooks_t* new_hooks = &my_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    if ((ret = mallctl("arena.0.extent_hooks", &old_hooks, &olen, &new_hooks, sizeof(new_hooks))))
        std::cerr << "mallctl arena.extent_hooks returned " << strerror(ret) << std::endl;

    //if ((ret = mallctl("thread.arena", nullptr, nullptr, &my_arena_index, sizeof(my_arena_index))))
    //    LOG_ERROR("mallctl thread.arena returned " << strerror(ret))
    //// now allocate a tiny block

    // Allocate using custom arena
    void* p1 = mallocx(32, 0);
    void* p2 = mallocx(100, 0);
    void* p3 = mallocx(200, 0);
    std::cout << "Allocated p1=" << p1 << ", p2=" << p2 << ", p3=" << p3 << "\n";

    dallocx(p1, 0);
    dallocx(p2, 0);
    dallocx(p3, 0);
    std::cout << "Freed p1, p2, p3\n";
}
