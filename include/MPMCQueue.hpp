#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>
#include <stdexcept>

// --------- MPMC Ring Buffer (Dmitry Vyukov style) ---------
template<typename T, size_t Capacity>
class MPMCRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");

    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    Cell buffer[Capacity];
    std::atomic<size_t> head;
    std::atomic<size_t> tail;

public:
    MPMCRingBuffer() : head(0), tail(0) {
        for (size_t i = 0; i < Capacity; ++i)
            buffer[i].sequence.store(i, std::memory_order_relaxed);
    }

    bool enqueue(const T& value) {
        size_t pos = head.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = buffer[pos & (Capacity - 1)];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (head.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell.data = value;
                    cell.sequence.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // full
            } else {
                pos = head.load(std::memory_order_relaxed); // retry
            }
        }
    }

    bool dequeue(T& result) {
        size_t pos = tail.load(std::memory_order_relaxed);
        while (true) {
            Cell& cell = buffer[pos & (Capacity - 1)];
            size_t seq = cell.sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                if (tail.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    result = cell.data;
                    cell.sequence.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                return false; // empty
            } else {
                pos = tail.load(std::memory_order_relaxed); // retry
            }
        }
    }
};

// --------- Lock-Free Fixed-Size Block Allocator ---------

//template <typename BlockType, size_t NumBlocks>
//class BlockAllocator {
//    static constexpr size_t BLOCK_SIZE = sizeof(BlockType);           // bytes per block
//
//    alignas(64) uint8_t memory[BLOCK_SIZE * NumBlocks];
//    MPMCRingBuffer<size_t, NumBlocks> freelist;
//
//public:
//    BlockAllocator() {
//        for (size_t i = 0; i < NumBlocks; ++i) {
//            bool ok = freelist.enqueue(i);
//            assert(ok && "Initialization failed to enqueue block");
//        }
//    }
//
//    BlockType* allocate() {
//        size_t blockIndex;
//        if (freelist.dequeue(blockIndex)) {
//            return (BlockType *)&memory[blockIndex * BLOCK_SIZE];
//        }
//        return nullptr; // No free block
//    }
//
//    void deallocate(void* ptr) {
//        uintptr_t base = reinterpret_cast<uintptr_t>(memory);
//        uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
//
//        if (p < base || p >= base + BLOCK_SIZE * NumBlocks || (p - base) % BLOCK_SIZE != 0) {
//            throw std::invalid_argument("Pointer does not belong to allocator");
//        }
//
//        size_t index = (p - base) / BLOCK_SIZE;
//        bool ok = freelist.enqueue(index);
//        assert(ok && "Double free or freelist overflow");
//    }
//
//    size_t blockSize() const { return BLOCK_SIZE; }
//    size_t totalBlocks() const { return NumBlocks; }
//};
