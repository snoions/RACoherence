#ifndef _THREAD_OPS_H_
#define _THREAD_OPS_H_

#include "cacheInfo.hpp"
#include "config.hpp"
#include "logManager.hpp"
#include "localCLTable.hpp"

class ThreadOps {
    //CXL mem shared data
    LogManager *log_mgrs;

    //node local data
    CacheInfo *cache_info;
    unsigned node_id;

    //thread local data
    VectorClock thread_clock;
    LocalCLTable dirty_cls;

    clock_t write_to_log(bool is_release);

    void help_consume(const VectorClock &target);

    void wait_for_consume(const VectorClock &target);

public:
    ThreadOps() = default;
    ThreadOps(LogManager *lmgrs, CacheInfo *cinfo, unsigned nid): log_mgrs(lmgrs), cache_info(cinfo), node_id(nid) {}
    ThreadOps &operator=(const ThreadOps &other) = default;

    inline const VectorClock &thread_release() {
#ifdef LOCAL_CL_TABLE_BUFFER
        while (dirty_cls.dump_buffer_to_table())
            write_to_log(false);
#endif
        clock_t clk_val = write_to_log(true);
        thread_clock.merge(node_id, clk_val);
        return thread_clock;
    }

    inline void thread_acquire(const VectorClock &clock) {
        thread_clock.merge(clock);
#ifdef USER_HELP_CONSUME
        help_consume(clock);
#else
        wait_for_consume(clock);
#endif
    }

    inline bool check_invalidate(char *addr) {
#ifndef EAGER_INVALIDATE
        return cache_info->invalid_cls.invalidate_if_dirty((uintptr_t)addr);
#endif
        return false;
    }

    inline void log_store(char *addr) {
        uintptr_t cl_addr = (uintptr_t)addr & CACHE_LINE_MASK;

        while (dirty_cls.insert(cl_addr) || dirty_cls.get_length_entry_count() != 0)
            write_to_log(false);
    }

};
#endif
