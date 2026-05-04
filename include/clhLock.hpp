/******************************************************************************
 * Copyright (c) 2014, Pedro Ramalhete, Andreia Correia
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Concurrency Freaks nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************
 */
#ifndef _CLH_LOCK_H_
#define _CLH_LOCK_H_

#include <atomic>
#include <stdlib.h>
#include <pthread.h>
#include <sched.h>

#include "vectorClock.hpp"

namespace RACoherence {


struct CLHNode
{
    std::atomic<bool> succ_must_wait;

    CLHNode(bool is_locked): succ_must_wait(is_locked) {}
};

template<template<typename> class Allocator=std::allocator>
class CLHLock;
template<template<typename> class Allocator=std::allocator>
class CLHSharedLock;

template<template<typename T> class Allocator>
class CLHLock {
    CLHNode * mynode;
    //char padding[56];  // To avoid false sharing with the tail
    std::atomic<CLHNode *> tail;
    Allocator<CLHNode> alloc;

public:
    CLHLock() {
        CLHNode *node = new(alloc.allocate(1)) CLHNode(false);
        mynode = node;
        std::atomic_store(&tail, node);
    }

    ~CLHLock() {
        alloc.deallocate(std::atomic_load(&tail), 1);
    }

    /*
     * locks the mutex for the current thread. will wait for other threads
     * that did the std::atomic_exchange() before this one.
     *
     * progress condition: blocking
     */
    void lock() {
        // create the new node locked by default, setting islocked=1
        CLHNode *node = new(alloc.allocate(1)) CLHNode(true);
        CLHNode *prev = std::atomic_exchange(&tail, node);

        // this thread's node is now in the queue, so wait until it is its turn
        bool prev_islocked = std::atomic_load_explicit(&prev->succ_must_wait, std::memory_order_relaxed);
        if (prev_islocked) {
            while (prev_islocked) {
                sched_yield();  // replace this with thrd_yield() if you use <threads.h>
                prev_islocked = std::atomic_load(&prev->succ_must_wait);
            }
        }
        // this thread has acquired the lock on the mutex and it is now safe to
        // cleanup the memory of the previous node.
        alloc.deallocate(prev, 1);

        // store mynode for unlock() to use. we could replace
        // this with a thread-local, not sure which is faster.
        mynode = node;
    }

    /*
     * pseudo try-lock function. check if the mutex is locked once, then does blocking lock
     *
     * progress condition: blocking
     */
    bool try_lock() {
        CLHNode *prev = std::atomic_load(&tail);

        // Optimistic Check: Is the lock currently held?
        if (std::atomic_load(&prev->succ_must_wait)) {
            return false;
        }

        lock();
        return true;
    }


    /*
     * Unlocks the mutex. Assumes that the current thread holds the lock on the
     * mutex.
     *
     * Progress Condition: Wait-Free Population Oblivious
     */
    void unlock() {
        // We assume that if this function was called, it is because this thread is
        // currently holding the lock, which means that self->mynode is pointing to
        // the current thread's mynode.
        if (mynode == NULL) {
            // ERROR: This will occur if unlock() is called without a lock()
            assert(false && "unlock without lock");
            return;
        }
        std::atomic_store(&mynode->succ_must_wait, false);
    }

};

template<template<typename> class Allocator>
class CLHSharedLock {
    using wMutex = CLHLock<Allocator>;

    wMutex wlock;
    char padding2[60]; // avoid false sharing
    std::atomic<int> active_readers{0};

public:
    void lock_shared() {
        wlock.lock();
        active_readers.fetch_add(1, std::memory_order_acquire);
        wlock.unlock();
    }

    void unlock_shared() {
        active_readers.fetch_sub(1, std::memory_order_release);
    }

    void lock() {
        wlock.lock();

        while (active_readers.load(std::memory_order_acquire) > 0) {
            sched_yield();
        }
    }

    void unlock() {
        wlock.unlock();
    }
};

} // RACoherence

#endif /* _CLH_MUTEX_H_ */
