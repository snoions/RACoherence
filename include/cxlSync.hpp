#ifndef _CXL_MUTEX_H_
#define _CXL_MUTEX_H_

#include <atomic>
#include <thread>
#include "clh_mutex.hpp"
#include "cxlMalloc.hpp"
#include "flushUtils.hpp"
#include "threadOps.hpp"
#include "utils.hpp"
#include "vectorClock.hpp"

extern thread_local ThreadOps *thread_ops;

template<typename T>
class CXLAtomic {
    struct InnerData {
        std::atomic<T> atomic_data;
        Monitor<VectorClock> clock;
    };

    InnerData *inner;

public:
    CXLAtomic(): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()) {}

    ~CXLAtomic() {
        inner->~InnerData();
        cxlhc_free(inner, sizeof(InnerData));
    }

    inline void store(T desired, std::memory_order order) {
        if (order == std::memory_order_seq_cst || order == std::memory_order_release) { 
#ifdef PROTOCOL_OFF
            flush_fence();
            inner->atomic_data.store(desired, order);
#else
            auto thread_clock = thread_ops->thread_release();

            LOG_DEBUG("thread " << std::this_thread::get_id() << " release at " << this << std::dec << ", thread clock=" <<thread_clock)

#ifdef LOCATION_CLOCK_MERGE
            inner->clock.mod([&](auto &self) {
                self.merge(thread_clock);
            });
            inner->atomic_data.store(desired, order);
#else
            inner->clock.mod([&](auto &self) {
                self = thread_clock;
                inner->atomic_data.store(desired, order);
            });
#endif
#endif
        }
        else
            inner->atomic_data.store(desired, order);
    };

    inline T load(std::memory_order order) {
#ifndef PROTOCOL_OFF
        if (order == std::memory_order_seq_cst || order == std::memory_order_acquire) { 
            char ret;
            auto clock = inner->clock.get([&](auto &self) {
                ret = inner->atomic_data.load(order);
                return self;
            });

            LOG_DEBUG("thread " << std::this_thread::get_id() << " acquire at " << this << std::dec << ", loc clock=" <<clock)

            thread_ops->thread_acquire(clock);
            return ret;
        }
        else
            return inner->atomic_data.load(order);
#else
        return inner->atomic_data.load(order);
#endif
    };

    inline T fetch_add(T arg, std::memory_order order) {
#ifndef PROTOCOL_OFF
        if (order == std::memory_order_seq_cst || order == std::memory_order_acquire || order == std::memory_order_release || order == std::memory_order_acq_rel) { 
            char ret;
            const VectorClock *clock;
            if (order == std::memory_order_seq_cst || order == std::memory_order_release || order == std::memory_order_acq_rel) { 
                auto thread_clock = thread_ops->thread_release();

                LOG_DEBUG("thread " << std::this_thread::get_id() << " release at " << this << std::dec << ", thread clock=" <<thread_clock)
                clock = inner->clock.mod([&](auto &self) {
                    ret = inner->atomic_data.fetch_add(arg, order);
                    self.merge(thread_clock);
                    return &self;
                });
            } else {
                clock = inner->clock.get([&](auto &self) {
                    ret = inner->atomic_data.fetch_add(arg, order);
                    return &self;
                });
            }

            if (order == std::memory_order_seq_cst || order == std::memory_order_acquire || order == std::memory_order_acq_rel) { 
                LOG_DEBUG("thread " << std::this_thread::get_id() << " acquire at " << this << std::dec << ", loc clock=" <<  *clock)

                thread_ops->thread_acquire(*clock);
            }
            return ret;
        }
        else
            return inner->atomic_data.fetch_add(arg, order);
#else
        if (order == std::memory_order_seq_cst || order == std::memory_order_release || order == std::memory_order_acq_rel)
            flush_fence();

        return inner->atomic_data.fetch_add(arg, order);
#endif
    };
};

class CXLMutex {
    struct InnerData{
        clh_mutex_t mutex;
        VectorClock clock;

        InnerData(): clock() {
            clh_mutex_init(&mutex);
        }
        ~InnerData() {
            clh_mutex_destroy(&mutex);
        }
    };

    InnerData *inner;

public:
    CXLMutex(): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()) {}

    ~CXLMutex() {
        inner->~InnerData();
        cxlhc_free(inner, sizeof(InnerData));
    }

    inline void lock() {
        clh_mutex_lock(&inner->mutex);

#ifndef PROTOCOL_OFF
        LOG_DEBUG("thread " << std::this_thread::get_id() << " lock at " << this << std::dec << ", loc clock=" << inner->clock)

        thread_ops->thread_acquire(inner->clock);
#endif
    };

    inline void unlock() {
#ifdef PROTOCOL_OFF
        flush_fence();
#else
        auto thread_clock = thread_ops->thread_release();

        LOG_DEBUG("thread " << std::this_thread::get_id() << " unlock at " << this << std::dec << ", thread clock=" << thread_clock)

#ifdef LOCATION_CLOCK_MERGE
        inner->clock.merge(thread_clock);
#else
        inner->clock = thread_clock;
#endif
#endif
        clh_mutex_unlock(&inner->mutex);
    };
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
            target.store(0, std::memory_order_seq_cst);
            phase.fetch_add(1, std::memory_order_seq_cst);
        } else {
            while (phase.load(std::memory_order_seq_cst) == local_phase) {
                std::this_thread::yield();
            }
        }
    }
};


#endif
