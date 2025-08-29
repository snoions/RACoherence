#include "memoryPool.hpp" // Updated header file name
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
