#ifndef _JEMALLOC_POOL_H_
#define _JEMALLOC_POOL_H_
#include "jemalloc/jemalloc.h"

class MemoryPoolBase {
protected:
    struct Block { Block* next; };

    struct PoolList {
        Block* free_list = nullptr;
        std::mutex mtx;
        size_t block_size = 0;

        PoolList() = default;
        PoolList(const PoolList&) = delete;
        PoolList& operator=(const PoolList&) = delete;
        PoolList(PoolList&&) = delete;
        PoolList& operator=(PoolList&&) = delete;
    };
};

template <size_t... BlockSizes>
class MemoryPool : private MemoryPoolBase {
public:
    static constexpr size_t pool_count = sizeof...(BlockSizes);
    using PoolArray = std::array<PoolList, pool_count>;

    MemoryPool(char* mem, size_t mem_size)
        : memory(mem), memory_size(mem_size)
    {
        initialize_pools();
    }

    void* alloc(size_t size, size_t /*alignment*/ = alignof(std::max_align_t)) {
        PoolList* pl = select_pool(size);
        if (!pl) return nullptr;
        std::lock_guard<std::mutex> lock(pl->mtx);
        if (!pl->free_list) return nullptr;
        Block* block = pl->free_list;
        pl->free_list = block->next;
        return block;
    }

    void free_block(void* ptr, size_t size) {
        if (!ptr) return;
        PoolList* pl = select_pool(size);
        if (!pl) return;
        std::lock_guard<std::mutex> lock(pl->mtx);
        Block* block = reinterpret_cast<Block*>(ptr);
        block->next = pl->free_list;
        pl->free_list = block;
    }

private:
    char* memory;
    size_t memory_size;
    PoolArray pools;

    static constexpr std::array<size_t, pool_count> block_sizes = { BlockSizes... };

    void initialize_pools() {
        size_t offset = 0;
        size_t bytes_per_pool = memory_size / pool_count;

        for (size_t i = 0; i < pool_count; ++i) {
            PoolList& pl = pools[i];
            pl.block_size = block_sizes[i];

            size_t blocks_per_pool = 0;
            if (pl.block_size != 0) {
                if (i == pool_count - 1) {
                    size_t remaining = (offset <= memory_size) ? (memory_size - offset) : 0;
                    blocks_per_pool = remaining / pl.block_size;
                } else {
                    blocks_per_pool = bytes_per_pool / pl.block_size;
                }
            }

            if (blocks_per_pool == 0) {
                pl.free_list = nullptr;
                continue;
            }

            size_t bytes_needed = blocks_per_pool * pl.block_size;
            if (offset + bytes_needed > memory_size) {
                if (offset >= memory_size) { pl.free_list = nullptr; continue; }
                blocks_per_pool = (memory_size - offset) / pl.block_size;
                bytes_needed = blocks_per_pool * pl.block_size;
                if (blocks_per_pool == 0) { pl.free_list = nullptr; continue; }
            }

            pl.free_list = reinterpret_cast<Block*>(memory + offset);
            Block* current = pl.free_list;
            for (size_t b = 1; b < blocks_per_pool; ++b) {
                current->next = reinterpret_cast<Block*>(memory + offset + b * pl.block_size);
                current = current->next;
            }
            current->next = nullptr;
            offset += bytes_needed;
        }
    }

    PoolList* select_pool(size_t size) {
        for (size_t i = 0; i < pool_count; ++i) {
            if (size <= block_sizes[i]) return &pools[i];
        }
        return nullptr;
    }
};

using CXLHCPool = MemoryPool<4 << 10, 4 << 20>;
CXLHCPool* cxlhc_pool;
unsigned cxlhc_arena_index;

// jemalloc hooks
static void* cxlhc_extent_alloc(extent_hooks_t* hooks, void* new_addr,
                             size_t size, size_t alignment,
                             bool* zero, bool* commit, unsigned arena_ind) {
    void* ptr = cxlhc_pool->alloc(size, alignment);
    if (ptr && zero) *zero = true;
    if (ptr && commit) *commit = true;
    return ptr;
}

static bool cxlhc_extent_dalloc(extent_hooks_t* hooks, void* addr, size_t size,
                             bool committed, unsigned arena_ind) {
    cxlhc_pool->free_block(addr, size);
    return true;
}

static extent_hooks_t cxlhc_hooks = {
    .alloc = cxlhc_extent_alloc,
    .dalloc = cxlhc_extent_dalloc,
    .commit = nullptr,
    .decommit = nullptr,
    .purge_lazy = nullptr,
    .purge_forced = nullptr,
    .split = nullptr,
    .merge = nullptr
};

static void test() {
    CXLHCPool pool(cxl_hc_buf + sizeof(LogManager[NODE_COUNT]), CXL_HC_RANGE);
    cxlhc_pool = &pool;
    
    int ret;
    // Set chunk size
    int chunk_size = 18;
    if ((ret = mallctl("opt.lg_chunk", nullptr, nullptr, &chunk_size, sizeof(int))))
        LOG_ERROR("mallctl opt.lg_chunk returned " << strerror(ret))
    // Create new arena
    size_t sz = sizeof(cxlhc_arena_index);
    if ((ret = mallctl("arenas.create", &cxlhc_arena_index, &sz, nullptr, 0)))
        LOG_ERROR("mallctl arena.create returned " << strerror(ret))
    // Assign hooks
    extent_hooks_t* new_hooks = &cxlhc_hooks;
    extent_hooks_t* old_hooks = nullptr;
    size_t olen = sizeof(old_hooks);
    if ((ret = mallctl(("arena." + std::to_string(cxlhc_arena_index) + ".extent_hooks").c_str(), &old_hooks, &olen, &new_hooks, sizeof(new_hooks))))
        LOG_ERROR("mallctl arena.extent_hooks returned " << strerror(ret))

    // now allocate a tiny block

    // Allocate using custom arena
    void* p1 = mallocx(32, MALLOCX_ARENA(cxlhc_arena_index));
    void* p2 = mallocx(100, MALLOCX_ARENA(cxlhc_arena_index));
    void* p3 = mallocx(200, MALLOCX_ARENA(cxlhc_arena_index));
    std::cout << "Allocated p1=" << p1 << ", p2=" << p2 << ", p3=" << p3 << "\n";

    dallocx(p1, MALLOCX_ARENA(cxlhc_arena_index));
    dallocx(p2, MALLOCX_ARENA(cxlhc_arena_index));
    dallocx(p3, MALLOCX_ARENA(cxlhc_arena_index));
    std::cout << "Freed p1, p2, p3\n";
}

#endif
