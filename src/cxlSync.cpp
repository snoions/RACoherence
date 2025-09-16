#include <atomic>
#include <thread>
#include "clh_mutex.hpp"
#include "cxlMalloc.hpp"
#include "cxlSync.hpp"
#include "flushUtils.hpp"
#include "threadOps.hpp"
#include "utils.hpp"
#include "vectorClock.hpp"

namespace RACoherence {

extern thread_local ThreadOps *thread_ops;

template<typename T>
CXLAtomic<T>::CXLAtomic(): inner(new(cxlhc_malloc(sizeof(InnerData))) InnerData()) {}

template<typename T>
CXLAtomic<T>::~CXLAtomic() {
    inner->~InnerData();
    cxlhc_free(inner, sizeof(InnerData));
}

template<typename T>
inline void store(T desired, std::memory_order order) {
    if (order == std::memory_order_seq_cst || order == std::memory_order_release) { 
ef PROTOCOL_OFF
        flush_fence();
        inner->atomic_data.store(desired, order);
e
        auto thread_clock = thread_ops->thread_release();

        LOG_DEBUG("thread " << std::this_thread::get_id() << " release at " << this << std::dec << ", thread clock=" <<thread_clock)

ef LOCATION_CLOCK_MERGE
        inner->clock.mod([&](auto &self) {
            self.merge(thread_clock);
        });
        inner->atomic_data.store(desired, order);
e
        inner->clock.mod([&](auto &self) {
            self = thread_clock;
            inner->atomic_data.store(desired, order);
        });
if
if
    }
    else
        inner->atomic_data.store(desired, order);
};

inline T load(std::memory_order order) {
def PROTOCOL_OFF
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
e
    return inner->atomic_data.load(order);
if
};

inline T fetch_add(T arg, std::memory_order order) {
def PROTOCOL_OFF
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
e
    if (order == std::memory_order_seq_cst || order == std::memory_order_release || order == std::memory_order_acq_rel)
        flush_fence();

    return inner->atomic_data.fetch_add(arg, order);
if
};

} // RACoherence
