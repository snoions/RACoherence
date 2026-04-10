#ifndef _CXL_MUTEX_H_
#define _CXL_MUTEX_H_

#include <atomic>
#include <thread>
#include "clh_mutex.hpp"
#include "clh_rwlock.hpp"
#include "cxlMalloc.hpp"
#include "flushUtils.hpp"
#include "threadOps.hpp"
#include "utils.hpp"
#include "vectorClock.hpp"

namespace RACoherence {

//TODO: try atomic clocks instead of lock-protected vector clocks
extern __thread ThreadOps *thread_ops;

template<typename T>
class CXLRelaxedAtomic {
public:
    struct InnerData {
        std::atomic<T> atomic_data;
    };
private:
    InnerData *inner;

public:
    CXLRelaxedAtomic(): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()) {}
    CXLRelaxedAtomic(InnerData *ptr): inner(new(ptr) InnerData()) {}

    ~CXLRelaxedAtomic() {
        inner->~InnerData();
        cxlhc_free(inner, sizeof(InnerData));
    }

    inline void store(T desired) {
        inner->atomic_data.store(desired, std::memory_order_relaxed);
    };

    inline T load() {
        return inner->atomic_data.load(std::memory_order_relaxed);
    };

    inline T fetch_add(T arg) {
        return inner->atomic_data.fetch_add(arg, std::memory_order_relaxed);
    };

    inline T exchange(T arg) {
        return inner->atomic_data.exchange(arg, std::memory_order_relaxed);
    };

    inline T compare_exchange_strong(T& expected, T desired) {
        return inner->atomic_data.compare_exchange_strong(expected, desired, std::memory_order_relaxed);
    };
};

template<typename T>
class CXLAtomic {
public:
    struct InnerData {
        std::atomic<T> atomic_data;
        VectorClock clock;
        CLHMutex mtx;
    };
private:
    InnerData *inner;

public:
    CXLAtomic(): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()) {}
    CXLAtomic(InnerData *ptr): inner(new(ptr) InnerData()) {}

    ~CXLAtomic() {
        inner->~InnerData();
        cxlhc_free(inner, sizeof(InnerData));
    }

    inline void store(T desired, std::memory_order order=std::memory_order_seq_cst) {
        if (order == std::memory_order_seq_cst || order == std::memory_order_release) { 
#if PROTOCOL_OFF
            writeback_fence();
            inner->atomic_data.store(desired, order);
#else
            thread_ops->thread_release();
            const VectorClock &thread_clock = thread_ops->get_clock();
            inner->mtx.lock();
#ifdef LOCATION_CLOCK_MERGE
            inner->clock.merge(thread_clock);
#else
            inner->clock = thread_clock;
#endif
            inner->atomic_data.store(desired, order);
            inner->mtx.unlock();
#endif
        }
        else
            inner->atomic_data.store(desired, order);
    };

    inline T load(std::memory_order order=std::memory_order_seq_cst) {
#if !PROTOCOL_OFF
        if (order == std::memory_order_seq_cst || order == std::memory_order_acquire) { 

            inner->mtx.lock();
            char ret = inner->atomic_data.load(order);
            VectorClock clock = inner->clock;
            inner->mtx.unlock();

            thread_ops->thread_acquire(clock);
            return ret;
        }
        else
            return inner->atomic_data.load(order);
#else
        return inner->atomic_data.load(order);
#endif
    };

    inline T fetch_add(T arg, std::memory_order order=std::memory_order_seq_cst) {
#if !PROTOCOL_OFF
        if (order == std::memory_order_seq_cst || order == std::memory_order_acquire || order == std::memory_order_release || order == std::memory_order_acq_rel) { 
            char ret;
            if (order == std::memory_order_seq_cst || order == std::memory_order_acq_rel) { 
                thread_ops->thread_release();
                inner->mtx.lock();
                ret = inner->atomic_data.fetch_add(arg, order);
                 inner->clock.merge(thread_ops->get_clock());
                const VectorClock clock = inner->clock;
                inner->mtx.unlock();
                thread_ops->thread_acquire(clock);
            } else if (order == std::memory_order_release) {
                thread_ops->thread_release();
                inner->mtx.lock();
                ret = inner->atomic_data.fetch_add(arg, order);
                inner->clock.merge(thread_ops->get_clock());
                inner->mtx.unlock();
            } else if (order == std::memory_order_acquire){
                inner->mtx.lock();
                ret = inner->atomic_data.fetch_add(arg, order);
                const VectorClock clock = inner->clock;
                inner->mtx.unlock();
                thread_ops->thread_acquire(clock);
            } else
                ret = inner->atomic_data.fetch_add(arg, order); 
            return ret;
        }
        else
            return inner->atomic_data.fetch_add(arg, order);
#else
        if (order == std::memory_order_seq_cst || order == std::memory_order_release || order == std::memory_order_acq_rel)
            writeback_fence();

        return inner->atomic_data.fetch_add(arg, order);
#endif
    };
};

// CXLRelaxedMutex only guarantees coherence of the contained data
template<typename T, size_t Count>
class CXLRelaxedMutex {
public:
    struct InnerData {
        CLHMutex mtx;
        unsigned owner_node = NODE_COUNT+1;
    };
private:
    InnerData *inner;
    T *data;

public:
    CXLRelaxedMutex(T *d): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()), data(d) {}
    CXLRelaxedMutex(InnerData *ptr, T *d): inner(new(ptr) InnerData()), data(d) {}

    ~CXLRelaxedMutex() {
        inner->~InnerData();
        cxlhc_free(inner, sizeof(InnerData));
    }

    inline void lock() {
#if CONSUME_HELP_IN_LOCK
        VectorClock clock = inner->clock;
        inner->mtx.lock_with_help(clock);
#else
        inner->mtx.lock();
#endif
#ifndef PROTOCOL_OFF
        unsigned nid = thread_op->get_node_id();
        if (inner->owner_node != nid) {
            do_range_invalidate((char *)&inner->data, Count * sizeof(T));
            inner->owner_node = nid;
            invalidate_fence();
        }
#else
        do_range_invalidate((char *)data, Count * sizeof(T));
        invalidate_fence();
#endif
    }

    inline void unlock() {
        do_range_writeback((char *)data, Count * sizeof(T));
        writeback_fence();
        inner->mtx.unlock();
    }

    inline T* get() {
        return data;
    }
};

class CXLMutex {
public:
    struct InnerData{
        CLHMutex mtx;
        VectorClock clock;
    };
private:
    InnerData *inner;

public:
    CXLMutex(): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()) {}
    CXLMutex(InnerData *ptr): inner(new(ptr) InnerData()) {}

    ~CXLMutex() {
        inner->~InnerData();
        cxlhc_free(inner, sizeof(InnerData));
    }

    inline void lock() {
#if CONSUME_HELP_IN_LOCK
        //TODO: wrap clock with atomics for thread safety
        VectorClock clock = inner->clock;
        inner->mtx.lock_with_help(clock);
#else
        inner->mtx.lock();
#endif

#if !PROTOCOL_OFF
        thread_ops->thread_acquire(inner->clock);
#endif
    }

    inline void unlock() {
#if PROTOCOL_OFF
        writeback_fence();
#else
        thread_ops->thread_release(); 
        const auto &thread_clock = thread_ops->get_clock();
#ifdef LOCATION_CLOCK_MERGE
        inner->clock.merge(thread_clock);
#else
        inner->clock = thread_clock;
#endif
#endif
        inner->mtx.unlock();
    }

    inline void unlock_relaxed() {
        inner->mtx.unlock();
    }
};

class CXLSharedMutex {
public:
    struct InnerData{
        CLHSharedMutex mtx;
        VectorClock clock;
    };
private:
    InnerData *inner;

public:
    CXLSharedMutex(): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()) {}
    CXLSharedMutex(void* ptr): inner(new(ptr) InnerData()) {}

    ~CXLSharedMutex() {
        inner->~InnerData();
        cxlhc_free(inner, sizeof(InnerData));
    }

    inline void lock() {
#if !PROTOCOL_OFF
#if CONSUME_HELP_IN_LOCK 
        VectorClock clock = inner->clock;
        inner->mtx.lock_with_help(clock);
#else
        inner->mtx.lock_shared();
        thread_ops->thread_acquire(inner->clock);
        inner->mtx.unlock_shared();
 
        inner->mtx.lock(); 
        thread_ops->thread_acquire(inner->clock);
#endif
#else
        inner->mtx.lock();
#endif
    }

    inline void lock_shared() {
#if CONSUME_HELP_IN_LOCK && !PROTOCOL_OFF
        VectorClock clock = inner->clock;
        inner->mtx.lock_shared_with_help(clock);
#else
        inner->mtx.lock_shared();
#endif

#if !PROTOCOL_OFF
        thread_ops->thread_acquire(inner->clock);
#endif
    }

    inline void unlock() {
#if PROTOCOL_OFF
        writeback_fence();
#else
        thread_ops->thread_release();
        const auto &thread_clock = thread_ops->get_clock();
#ifdef LOCATION_CLOCK_MERGE
        inner->clock.merge(thread_clock);
#else
        inner->clock = thread_clock;
#endif
#endif
        inner->mtx.unlock();
    }

    inline void unlock_shared() {
#if PROTOCOL_OFF
        writeback_fence();
#else
        thread_ops->thread_release();
        const auto &thread_clock = thread_ops->get_clock();
#ifdef LOCATION_CLOCK_MERGE
        inner->clock.merge(thread_clock);
#else
        inner->clock = thread_clock;
#endif
#endif
        inner->mtx.unlock_shared();
    }
};

class CXLBarrier {
    CXLAtomic<int> target;
    CXLAtomic<int> arrived;
    CXLAtomic<int> phase;

public:
    CXLBarrier() = default;

    CXLBarrier(int count) {
        init(count);
    }

    inline void init(int count) {
        target.store(count, std::memory_order_seq_cst);
        arrived.store(0, std::memory_order_seq_cst);
        phase.store(0, std::memory_order_seq_cst);
    }

    inline void wait() {
        int local_phase = phase.load(std::memory_order_seq_cst);

        int local_arrived = arrived.fetch_add(1, std::memory_order_seq_cst) + 1;

        if (local_arrived == target.load(std::memory_order_seq_cst)) {
            arrived.store(0, std::memory_order_seq_cst);
            phase.fetch_add(1, std::memory_order_seq_cst);
        } else {
            while (phase.load(std::memory_order_seq_cst) == local_phase) {
                std::this_thread::yield();
            }
        }
    }
};

} // RACoherence

#endif
