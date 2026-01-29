#ifndef _EXTENT_POOL_H_
#define _EXTENT_POOL_H_

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unistd.h>
#include "jemalloc/jemalloc.h"
#include "config.hpp"

namespace RACoherence {

/* * ExtentPool - Lock-free, linear bump allocator.
 * * This allocator strictly moves forward. Memory is never reclaimed or reused.
 * It is thread-safe using std::atomic operations.
 */
class ExtentPool {
public:
    ExtentPool(void* buffer, size_t buffer_size)
        : base_(reinterpret_cast<uint8_t*>(buffer)), size_(buffer_size), offset_(0) {}

    // Allocate exact (size, alignment) using atomic bump pointer.
    // Alignment must be a power of 2.
    void* allocate(size_t size, size_t alignment) {
        // Load current offset atomically
        size_t cur = offset_.load(std::memory_order_relaxed);
        uintptr_t base_addr = reinterpret_cast<uintptr_t>(base_);

        while (true) {
            uintptr_t start_addr = base_addr + cur;
            
            // Calculate aligned address
            // Formula: (addr + align - 1) & ~(align - 1)
            uintptr_t aligned_addr = start_addr;
            if (alignment > 1) {
                aligned_addr = (start_addr + (alignment - 1)) & ~(alignment - 1);
            }

            // Calculate the actual offset relative to base required for this alignment
            size_t aligned_offset = static_cast<size_t>(aligned_addr - base_addr);

            // Check if we have enough space
            if (aligned_offset + size > size_) {
                return nullptr; // Out of memory
            }

            // Calculate the next offset state (end of this allocation)
            size_t next = aligned_offset + size;

            // Attempt to update the offset atomically
            // memory_order_acq_rel ensures that if we succeed, no prior reads/writes 
            // from other threads interfere with our claim on this memory block.
            if (offset_.compare_exchange_weak(cur, next,
                                              std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
                // Success: return the aligned pointer
                return reinterpret_cast<void*>(aligned_addr);
            }

            // If CAS failed, 'cur' is automatically updated to the current value 
            // of 'offset_', and the loop retries.
        }
    }

    // Deallocate is a no-op.
    void deallocate(void* ptr, size_t size) {
        (void)ptr;
        (void)size;
    }

    size_t total_size() const { return size_; }

private:
    uint8_t* base_;
    size_t size_;
    
    // Atomic offset acts as the 'bump pointer'
    // Aligned to cache line size to prevent false sharing if multiple pools exist
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> offset_; 
};

} // RACoherence

#endif
