#ifndef _MEMORY_POOL_H_
#define _MEMORY_POOL_H_

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <cassert>
#include <type_traits>

#include "logger.hpp"

namespace RACoherence {

class AlignUtil {
public:
    static size_t align_up(size_t n, size_t align) {
        return (n + align - 1) & ~(align - 1);
    }
};

// Tagged pointer pool: pack (pointer | tag) in 64-bit word.
// Assumptions: pointer fits in LOWER PTR_BITS bits.
template <size_t... BlockSizes>
class MemoryPool {
    static_assert(sizeof...(BlockSizes) > 0, "At least one block size required");
    static_assert(std::is_same_v<void*, void*>, "pointer type sanity");

    struct Node { Node* next; };

public:
    static constexpr size_t bucket_count = sizeof...(BlockSizes);
    static constexpr std::array<size_t, bucket_count> block_sizes = { BlockSizes... };

    // Tag bits: choose how many bits for tag. Here 16 gives 48-bit pointer space (common on x86_64).
    static constexpr unsigned TAG_BITS = 16u;
    static constexpr unsigned PTR_BITS = 64u - TAG_BITS;
    static_assert(TAG_BITS > 0 && PTR_BITS > 0, "invalid tag/pointer bit split");

    MemoryPool(void* buf, size_t buf_size)
      : base(reinterpret_cast<uint8_t*>(buf)), base_size(buf_size)
    {
        // Sanity checks for block sizes
        for (size_t i = 0; i < bucket_count; ++i) {
            if (block_sizes[i] < sizeof(Node)) {
                std::cerr << "MemoryPool error: Block size " << block_sizes[i]
                          << " < sizeof(Node) (" << sizeof(Node) << ")\n";
                std::exit(EXIT_FAILURE);
            }
        }

        // Ensure we can pack pointers into PTR_BITS
        uintptr_t test_ptr = reinterpret_cast<uintptr_t>(base);
        if (!pointer_fits(test_ptr)) {
            std::cerr << "MemoryPool fatal: pool base pointer " << (void*)base
                      << " does not fit into " << PTR_BITS << " bits required for tagging.\n";
            std::exit(EXIT_FAILURE);
        }

        // init heads and counts
        for (size_t i = 0; i < bucket_count; ++i) {
            heads_tagged[i].store(pack(nullptr, 0), std::memory_order_relaxed);
            counts[i].store(0, std::memory_order_relaxed);
        }

        init_buckets();
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    void* allocate(size_t size) {
        size_t idx = bucket_index_for(size);
        if (idx == SIZE_MAX) {
            LOG_ERROR("allocate: out of memory")
        }
        Node* n = pop_tagged(idx);
        return reinterpret_cast<void*>(n);
    }

    void deallocate(void* p, size_t size) {
        if (!p) return;
        if (!pointer_in_range(p)) {
            LOG_ERROR("deallocate: pointer " << p << " is not in pool range\n")
        }
        size_t idx = bucket_index_for(size);
        if (idx == SIZE_MAX) {
            std::cerr << "MemoryPool::deallocate: invalid size " << size << "\n";
        }
        push_tagged(idx, reinterpret_cast<Node*>(p));
    }

    size_t free_count(size_t bucket_idx) const {
        return counts[bucket_idx].load(std::memory_order_relaxed);
    }

    void debug_print() const {
        std::cout << "MemoryPool debug:\n";
        for (size_t i = 0; i < bucket_count; ++i) {
            std::cout << "  bucket " << i << " block_size=" << block_sizes[i]
                      << " free=" << free_count(i) << "\n";
        }
    }

private:
    uint8_t* base;
    size_t base_size;

    // tagged head: lower PTR_BITS bits store pointer, top TAG_BITS store tag
    std::array<std::atomic<uint64_t>, bucket_count> heads_tagged;
    std::array<std::atomic<size_t>, bucket_count> counts;

    // Helpers for packing/unpacking
    static constexpr uint64_t PTR_MASK = (PTR_BITS == 64) ? ~uint64_t(0) : ((uint64_t(1) << PTR_BITS) - 1);
    static constexpr uint64_t TAG_SHIFT = PTR_BITS;

    static inline bool pointer_fits(uintptr_t p) {
        return ( (uint64_t)p & ~PTR_MASK ) == 0;
    }

    static inline uint64_t pack(Node* p, uint64_t tag) {
        uintptr_t up = reinterpret_cast<uintptr_t>(p);
        if (!pointer_fits(up)) {
            // will be caught earlier; but guard anyway
            std::cerr << "MemoryPool::pack: pointer too large to pack\n";
            std::exit(EXIT_FAILURE);
        }
        uint64_t raw = (uint64_t)up | ((tag & ((uint64_t(1) << TAG_BITS) - 1)) << TAG_SHIFT);
        return raw;
    }

    static inline Node* unpack_ptr(uint64_t raw) {
        uint64_t pbits = raw & PTR_MASK;
        return reinterpret_cast<Node*>(uintptr_t(pbits));
    }

    static inline uint64_t unpack_tag(uint64_t raw) {
        return raw >> TAG_SHIFT;
    }

    // init buckets (slice buffer)
    void init_buckets() {
        constexpr size_t alignment = alignof(std::max_align_t);
        size_t offset = 0;
        size_t bytes_per_bucket = base_size / bucket_count;

        for (size_t i = 0; i < bucket_count; ++i) {
            size_t bsize = block_sizes[i];
            if (bsize == 0) continue;
            offset = AlignUtil::align_up(offset, alignment);
            size_t bucket_bytes = (i == bucket_count - 1) ? (base_size - offset) : bytes_per_bucket;
            size_t blocks = bucket_bytes / bsize;
            if (blocks == 0) {
                heads_tagged[i].store(pack(nullptr, 0), std::memory_order_relaxed);
                counts[i].store(0, std::memory_order_relaxed);
                continue;
            }
            size_t bytes_needed = blocks * bsize;
            if (offset + bytes_needed > base_size) {
                std::cerr << "MemoryPool::init_buckets: computed overflow\n";
                std::exit(EXIT_FAILURE);
            }

            Node* head = reinterpret_cast<Node*>(base + offset);
            Node* cur = head;
            for (size_t k = 1; k < blocks; ++k) {
                Node* next = reinterpret_cast<Node*>(base + offset + k * bsize);
                cur->next = next;
                cur = next;
            }
            cur->next = nullptr;

            // pack pointer with initial tag 0
            uint64_t raw = pack(head, 0);
            heads_tagged[i].store(raw, std::memory_order_relaxed);
            counts[i].store(blocks, std::memory_order_relaxed);
            offset += bytes_needed;
        }
    }

    static constexpr size_t bucket_index_for(size_t size) {
        for (size_t i = 0; i < bucket_count; ++i) {
            if (size <= block_sizes[i]) return i;
        }
        return SIZE_MAX; // custom sentinel
    }

    bool pointer_in_range(void* p) const {
        uintptr_t up = reinterpret_cast<uintptr_t>(p);
        uintptr_t b = reinterpret_cast<uintptr_t>(base);
        return (up >= b) && (up + 1 <= b + base_size) && pointer_fits(up);
        // note: +1 bound check is cheap guard; we assume p points to start of block
    }

    // --- Tag-protected push/pop ---

    void push_tagged(size_t idx, Node* node) {
        // node->next will be set to the current head pointer on each attempt
        uint64_t old_raw = heads_tagged[idx].load(std::memory_order_relaxed);
        for (;;) {
            Node* old_ptr = unpack_ptr(old_raw);
            uint64_t old_tag = unpack_tag(old_raw);
            node->next = old_ptr;
            uint64_t new_raw = pack(node, old_tag + 1);
            // attempt CAS: on success, release semantics to publish node->next
            if (heads_tagged[idx].compare_exchange_weak(old_raw, new_raw,
                                                        std::memory_order_release,
                                                        std::memory_order_relaxed)) {
                counts[idx].fetch_add(1, std::memory_order_relaxed);
                return;
            }
            // CAS failed: old_raw updated with current head; retry (node->next will be set again)
        }
    }

    Node* pop_tagged(size_t idx) {
        uint64_t old_raw = heads_tagged[idx].load(std::memory_order_acquire);
        while (old_raw != 0) {
            Node* old_ptr = unpack_ptr(old_raw);
            if (!pointer_in_range(old_ptr)) {
                std::cerr << "MemoryPool::pop_tagged: head pointer invalid: " << old_ptr << "\n";
            }
            Node* next = old_ptr->next;
            uint64_t old_tag = unpack_tag(old_raw);
            uint64_t new_raw = pack(next, old_tag + 1);
            // attempt CAS; success must have acquire semantics to synchronize with push's release
            if (heads_tagged[idx].compare_exchange_weak(old_raw, new_raw,
                                                        std::memory_order_acq_rel,
                                                        std::memory_order_acquire)) {
                counts[idx].fetch_sub(1, std::memory_order_relaxed);
                return old_ptr;
            }
            // CAS failed; old_raw updated with current head; retry
        }
        return nullptr;
    }
};

} // RACoherence

#endif
