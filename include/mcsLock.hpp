#ifndef _MCS_LOCK_
#define _MCS_LOCK_

#include <atomic>
#include <sched.h>

#include "config.hpp"
#include "cxlMalloc.hpp"

namespace RACoherence {

struct MCSNode {
    std::atomic<MCSNode*> next{nullptr};
    std::atomic<bool> locked{false};
};

class MCSLock {
private:
    std::atomic<MCSNode*> tail{nullptr};
    //char padding[56]; // avoid false sharing
    MCSNode* owner = nullptr;
public:
    friend class MCSSharedLock;
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
        MCSNode *node = (MCSNode*)cxlhc_malloc(sizeof(MCSNode));
        lock_with_node(node);
    }

    bool try_lock() {
        if (tail.load(std::memory_order_acquire))
            return false;

        MCSNode *node = (MCSNode*)cxlhc_malloc(sizeof(MCSNode));
        node->next.store(nullptr, std::memory_order_relaxed);
        node->locked.store(true, std::memory_order_relaxed);

        MCSNode* expected = nullptr;
         
        if (tail.compare_exchange_strong(expected, node, 
                                         std::memory_order_acq_rel, 
                                         std::memory_order_relaxed)) {

            owner = node;
            return true;
        }

        cxlhc_free(node, sizeof(MCSNode));
        return false;
    }

    void unlock() {
        MCSNode *node = owner;
        unlock_with_node(node);
        cxlhc_free(node, sizeof(MCSNode));
    }
};

class MCSSharedLock {
private:
    MCSLock wlock;
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
        MCSNode *node = (MCSNode*)cxlhc_malloc(sizeof(MCSNode));
        lock_shared_with_node(node);
        cxlhc_free(node, sizeof(MCSNode));
    }
    
    void lock() {
        MCSNode *node = (MCSNode*)cxlhc_malloc(sizeof(MCSNode));
        lock_with_node(node);
    }

    void unlock() {
        MCSNode *node = wlock.owner;
        unlock_with_node(node);
        cxlhc_free(node, sizeof(MCSNode));
    }
};

struct defer_lock_t { explicit defer_lock_t() = default; };

constexpr defer_lock_t defer_lock{};

// Unique lock allocates node on the stack
// cannot be used for locks shared between hosts
class UniqueLock {
    MCSNode node;
    MCSLock &lk;
    bool locked;

public:

    UniqueLock(MCSLock &l): lk(l), locked(true) {
        lk.lock_with_node(&node);
    }
    
    UniqueLock(MCSLock &l, defer_lock_t /*d*/): lk(l), locked(false) {}
    
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
