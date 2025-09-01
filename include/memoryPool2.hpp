#ifndef _CUSTOM_POOL_H_
#define _CUSTOM_POOL_H_


#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>
#include <cstring>

class AlignUtil {
public:
    static size_t align_up(size_t n, size_t align) {
        return (n + align - 1) & ~(align - 1);
    }
};

template <size_t... BlockSizes>
class MemoryPool {
    static_assert(sizeof...(BlockSizes) > 0, "At least one block size required");

    // internal node stored inside each block
    struct Node {
        Node* next;
    };

public:
    static constexpr size_t bucket_count = sizeof...(BlockSizes);
    static constexpr std::array<size_t, bucket_count> block_sizes = { BlockSizes... };

    // Construct with a buffer and its size. Buffer must outlive pool.
    MemoryPool(void* buf, unsigned buf_size): base(reinterpret_cast<char*>(buf)), base_size(buf_size) {
        for (size_t i = 0; i < bucket_count; ++i) {
            heads[i].store(nullptr, std::memory_order_relaxed);
            counts[i].store(0, std::memory_order_relaxed);
        }
        init_buckets();
    }

    // Non-copyable/movable
    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // Allocate block of <= size. Returns pointer or nullptr if exhausted/no bucket.
    void* allocate(size_t size) {
        size_t idx = bucket_index_for(size);
        if (idx == SIZE_MAX) return nullptr;
        return pop(idx);
    }

    // Return block (must have been allocated from this pool and size should match or be <= block_size).
    void deallocate(void* p, size_t size) {
        if (!p) return;
        size_t idx = bucket_index_for(size);
        if (idx == SIZE_MAX) return; // invalid
        push(idx, p);
    }

    // stats
    size_t free_count(size_t bucket_idx) const {
        return counts[bucket_idx].load(std::memory_order_relaxed);
    }

    size_t total_bytes() const { return base_size; }

    void debug_print() const {
        std::cout << "MemoryPool debug:\n";
        size_t offset = 0;
        for (size_t i = 0; i < bucket_count; ++i) {
            std::cout << "  bucket " << i << " block_size=" << block_sizes[i]
                      << " free=" << free_count(i) << "\n";
        }
    }

private:
    char* base;
    size_t base_size;

    // per-bucket atomic head pointer for linked free list
    std::array<std::atomic<Node*>, bucket_count> heads;
    // approximate counts for debugging
    std::array<std::atomic<size_t>, bucket_count> counts;

    // initialize buckets by slicing the memory buffer into pieces for each block size
    void init_buckets() {
        // We'll slice buffer in order. Ensure each block is suitably aligned.
        constexpr size_t alignment = alignof(std::max_align_t);
        size_t offset = 0;
        size_t bytes_per_bucket = base_size / bucket_count;

        for (size_t i = 0; i < bucket_count; ++i) {
            size_t bsize = block_sizes[i];
            if (bsize == 0) continue;
            // align the start for safety
            offset = AlignUtil::align_up(offset, alignment);
            size_t bucket_bytes = (i == bucket_count - 1) ? (base_size - offset) : bytes_per_bucket;
            size_t blocks = bucket_bytes / bsize;
            if (blocks == 0) {
                heads[i].store(nullptr, std::memory_order_relaxed);
                counts[i].store(0, std::memory_order_relaxed);
                continue;
            }
            Node* head = reinterpret_cast<Node*>(base + offset);
            Node* cur = head;
            for (size_t k = 1; k < blocks; ++k) {
                Node* next = reinterpret_cast<Node*>(base + offset + k * bsize);
                cur->next = next;
                cur = next;
            }
            cur->next = nullptr;
            heads[i].store(head, std::memory_order_relaxed);
            counts[i].store(blocks, std::memory_order_relaxed);
            offset += blocks * bsize;
        }
    }

    // find smallest bucket index that fits 'size'
    static constexpr size_t bucket_index_for(size_t size) {
        for (size_t i = 0; i < bucket_count; ++i) {
            if (size <= block_sizes[i]) return i;
        }
        return SIZE_MAX;
    }

    // lock-free pop (Treiber stack)
    void* pop(size_t idx) {
        Node* old = heads[idx].load(std::memory_order_acquire);
        while (old) {
            Node* next = old->next;
            if (heads[idx].compare_exchange_weak(old, next,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire)) {
                counts[idx].fetch_sub(1, std::memory_order_relaxed);
                return reinterpret_cast<void*>(old);
            }
            // CAS failed, old has been updated with new value -> retry
        }
        return nullptr; // empty
    }

    // lock-free push
    void push(size_t idx, void* p) {
        Node* node = reinterpret_cast<Node*>(p);
        Node* old = heads[idx].load(std::memory_order_acquire);
        do {
            //poison
            node->next = old;
        } while (!heads[idx].compare_exchange_weak(old, node,
                    std::memory_order_release,
                    std::memory_order_acquire));
        counts[idx].fetch_add(1, std::memory_order_relaxed);
    }
};

#endif
