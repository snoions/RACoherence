#ifndef _CXL_MUTEX_H_
#define _CXL_MUTEX_H_

#include <atomic>
#include "clh_mutex.hpp"
#include "cxlMalloc.hpp"
//#include "malloc.hpp"
#include "threadOps.hpp"
#include "utils.hpp"
#include "vectorClock.hpp"

//extern mspace cxl_hc_space;
extern thread_local ThreadOps *thread_ops;

template<typename T>
class CXLAtomic {
    struct InnerData {
        std::atomic<T> atomic_data;
        Monitor<VectorClock> clock;

        InnerData() = default;
    };

    InnerData *inner;

public:
    // CXLAtomic(): inner(new(mspace_malloc(cxl_hc_space, sizeof(InnerData))) InnerData()) 
    CXLAtomic(): inner(new(hc_malloc(sizeof(InnerData))) InnerData()) {}

    ~CXLAtomic() {
        inner->~InnerData();
        //mspace_free(cxl_hc_space, inner);
        hc_free(inner, sizeof(InnerData));
    }

    inline void store(T desired, std::memory_order order) {
        if (order == std::memory_order_seq_cst || order == std::memory_order_release) { 
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
        }
        else
            inner->atomic_data.store(desired, order);
    };

    inline T load(std::memory_order order) {
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
    //CXLMutex(): inner(new(mspace_malloc(cxl_hc_space, sizeof(InnerData))) InnerData()) 
    CXLMutex(): inner(new(hc_malloc(sizeof(InnerData))) InnerData()) {}

    ~CXLMutex() {
        inner->~InnerData();
        //mspace_free(cxl_hc_space, inner);
        hc_free(inner, sizeof(InnerData));
    }

    void lock() {
        clh_mutex_lock(&inner->mutex);

        LOG_DEBUG("thread " << std::this_thread::get_id() << " lock at " << this << std::dec << ", loc clock=" << inner->clock)

        thread_ops->thread_acquire(inner->clock);
    };

    void unlock() {
        auto thread_clock = thread_ops->thread_release();

        LOG_DEBUG("thread " << std::this_thread::get_id() << " unlock at " << this << std::dec << ", thread clock=" << thread_clock)

#ifdef LOCATION_CLOCK_MERGE
        inner->clock.merge(thread_clock);
#else
        inner->clock = thread_clock;
#endif
        clh_mutex_unlock(&inner->mutex);
    };
};

#endif
