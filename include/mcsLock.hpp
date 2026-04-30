#ifndef _MCS_LOCK_
#define _MCS_LOCK_

#include <atomic>
#include <sched.h>
#include <memory>

#include "config.hpp"

namespace RACoherence {

struct MCSNode {
    std::atomic<MCSNode*> next{nullptr};
    std::atomic<bool> locked{false};
};

template<template<typename> class Allocator=std::allocator>
class MCSLock;
template<template<typename> class Allocator=std::allocator>
class MCSSharedLock;

template<template<typename> class Allocator>
class MCSLock {
private:
    std::atomic<MCSNode*> tail{nullptr};
    //char padding[56]; // avoid false sharing
    MCSNode* owner = nullptr;
    Allocator<MCSNode> alloc;

public:
    friend class MCSSharedLock<Allocator>;

    void lock_with_node(MCSNode* node) {
        node->next.store(nullptr, std::memory_order_relaxed);
        node->locked.store(true, std::memory_order_relaxed);

        MCSNode* predecessor = tail.exchange(node, std::memory_order_acq_rel);

        if (predecessor != nullptr) {
            predecessor->next.store(node, std::memory_order_release);
            while (node->locked.load(std::memory_order_acquire)) {
                sched_yield();
            }
        }
        owner = node;
    }

    bool try_lock_with_node(MCSNode* node) {
        if (tail.load(std::memory_order_acquire))
            return false;

        node->next.store(nullptr, std::memory_order_relaxed);
        node->locked.store(true, std::memory_order_relaxed);

        MCSNode* expected = nullptr;
         
        if (tail.compare_exchange_strong(expected, node, 
                                         std::memory_order_acq_rel, 
                                         std::memory_order_relaxed)) {

            owner = node;
            return true;
        }

        return false;
    }

    void unlock_with_node(MCSNode* node) {
        MCSNode* next = node->next.load(std::memory_order_acquire);
        
        if (next == nullptr) {
            MCSNode* expected = node;
            if (tail.compare_exchange_strong(expected, nullptr, 
                                             std::memory_order_release, 
                                             std::memory_order_relaxed)) {
                return;
            }

            while ((next = node->next.load(std::memory_order_acquire)) == nullptr) {
                sched_yield();
            }
        }
        next->locked.store(false, std::memory_order_release);
    }

    void lock() {
        MCSNode *node = alloc.allocate(1);
        lock_with_node(node);
    }

    bool try_lock() {
        if (tail.load(std::memory_order_acquire))
            return false;

        MCSNode *node = alloc.allocate(1);
        node->next.store(nullptr, std::memory_order_relaxed);
        node->locked.store(true, std::memory_order_relaxed);

        MCSNode* expected = nullptr;
         
        if (tail.compare_exchange_strong(expected, node, 
                                         std::memory_order_acq_rel, 
                                         std::memory_order_relaxed)) {

            owner = node;
            return true;
        }

        alloc.deallocate(node, 1);
        return false;
    }

    void unlock() {
        MCSNode *node = owner;
        unlock_with_node(node);
        alloc.deallocate(node, 1);
    }
};

template<template<typename> class Allocator>
class MCSSharedLock {
private:
    using wMutex = MCSLock<Allocator>;

    wMutex wlock;
    char padding2[60]; // avoid false sharing
    std::atomic<int> active_readers{0};

public:
    void lock_shared_with_node(MCSNode* node) {
        wlock.lock_with_node(node);
        active_readers.fetch_add(1, std::memory_order_acquire);
        wlock.unlock_with_node(node);
    }

    void unlock_shared() {
        active_readers.fetch_sub(1, std::memory_order_release);
    }

    void lock_with_node(MCSNode* node) {
        wlock.lock_with_node(node);

        while (active_readers.load(std::memory_order_acquire) > 0) {
            sched_yield();
        }
    }

    void unlock_with_node(MCSNode* node) {
        wlock.unlock_with_node(node);
    }

    void lock_shared() {
        MCSNode *node = wlock.alloc.allocate(1);
        lock_shared_with_node(node);
        wlock.alloc.deallocate(node, 1);
    }
    
    void lock() {
        MCSNode *node = wlock.alloc.allocate(1);
        lock_with_node(node);
    }

    void unlock() {
        MCSNode *node = wlock.owner;
        unlock_with_node(node);
        wlock.alloc.deallocate(node, 1);
    }
};

struct defer_Mutex { explicit defer_Mutex() = default; };

constexpr defer_Mutex defer_lock{};

// Unique lock allocates node on the stack
// cannot be used for locks shared between hosts

template<template<typename> class Allocator=std::allocator>
class UniqueLock {
    using Mutex = MCSLock<Allocator>;

    MCSNode node;
    Mutex &lk;
    bool locked;

public:

    UniqueLock(Mutex &l): lk(l), locked(true) {
        lk.lock_with_node(&node);
    }
    
    UniqueLock(Mutex &l, defer_Mutex /*d*/): lk(l), locked(false) {}
    
    ~UniqueLock() {
        if (locked)
            lk.unlock_with_node(&node);
    }
    
    void lock() {
        lk.lock_with_node(&node);
        locked = true;
    }

    void unlock() {
        lk.unlock_with_node(&node);
        locked = false;
    }

    bool try_lock() {
        locked = lk.try_lock_with_node(&node);
        return locked;
    }
};
} // RACoherence

#endif
