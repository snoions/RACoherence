#ifndef _THREAD_OPS_H_
#define _THREAD_OPS_H_

#include <unistd.h>

#include "cacheInfo.hpp"
#include "config.hpp"
#include "logManager.hpp"
#include "localCLTable.hpp"

namespace RACoherence {

class ThreadOps {
    //CXL mem shared data
    LogManager *log_mgrs;

    //node local data
    CacheInfo *cache_info;
    unsigned node_id;
    unsigned thread_id;

    //thread local data
    VectorClock thread_clock;
    LocalCLTable dirty_cls;
    uintptr_t recent_cl;

    clock_t write_to_log(bool is_release) {
        Log *curr_log;
        while(!(curr_log = log_mgrs[node_id].get_new_log())) {
            STATS(blocked_count++)
            sched_yield();
        }
        using namespace cl_group;

        for(auto cg: dirty_cls) {
            if (!cg)
                continue;
            curr_log->write(cg);
            if (is_length_based(cg)) {
                for (auto cl_addr: LengthCLRange(cg))
                    // should be unrolled, manually unroll if not
                    for (unsigned i = 0; i < GROUP_SIZE * CL_UNIT_GRANULARITY; i++)
                        do_flush((char *)cl_addr + (i * CACHE_LINE_SIZE));
            } else {
                for (auto cl_addr: MaskCLRange(get_ptr(cg), get_mask16(cg)))
                    // should be unrolled, manually unroll if not
                    for (unsigned i = 0; i < CL_UNIT_GRANULARITY; i++)
                        do_flush((char *)cl_addr + i * CACHE_LINE_SIZE);
            }
        }

        flush_fence();
        clock_t clk_val = log_mgrs[node_id].produce_tail(curr_log, is_release);
        dirty_cls.clear_table();
        LOG_DEBUG("node " << node_id << " produce log " << cache_info->produced_count++)
        return clk_val;
    }

    void help_consume(const VectorClock &target) {
        for (unsigned i=0; i<NODE_COUNT; i=(i+1==node_id)? i+2: i+1) {
            auto val = cache_info->get_clock(i);
            if (val >= target[i])
                continue;

            std::unique_lock<std::mutex> l(log_mgrs[i].get_head_mutex(node_id));
            //check again after wake up
            val = cache_info->get_clock(i);

            while(val < target[i]) {
                Log *log;
                //log might be null here because of yet to be produced loogs before the produced target log
                while(!(log = log_mgrs[i].take_head(node_id)));
                cache_info->process_log(*log);
                if (log->is_release()) {
                    val = log->get_log_idx();
                    cache_info->update_clock(i, val);
                }
                log_mgrs[i].consume_head(node_id);
                LOG_DEBUG("node " << node_id << " consume log " << ++cache_info->consumed_count[i] << " from " << i)
            }

        }
    }

    void wait_for_consume(const VectorClock &target) {
        while(true) {
            bool uptodate = true;
            for (unsigned i=0; i<NODE_COUNT; i++) {
                auto curr = cache_info->get_clock(i);
                if (i != node_id && curr < target[i]) {
                    uptodate = false;
                    LOG_DEBUG("node " << node_id << " block on acquire, index=" << i << ", target=" << target[i] << ", current=" << curr)
                    break;
                }
            }
            if (uptodate)
                break;
            sched_yield();
        }
    }

public:
    ThreadOps() = default;
    ThreadOps(LogManager *lmgrs, CacheInfo *cinfo, unsigned nid, unsigned tid): log_mgrs(lmgrs), cache_info(cinfo), node_id(nid), thread_id(tid), recent_cl(0) {}
    ThreadOps &operator=(const ThreadOps &other) = default;

    unsigned get_node_id() { return node_id; }
    unsigned get_thread_id() { return thread_id; }
    const VectorClock &get_clock() {return thread_clock; }

    inline const VectorClock &thread_release() {
        LOG_DEBUG("thread " << std::this_thread::get_id() << " release at " << this << std::dec << ", thread clock=" <<thread_clock)
        if (!recent_cl)
            return thread_clock;
        recent_cl = 0;
#ifdef LOCAL_CL_TABLE_BUFFER
        while (dirty_cls.dump_buffer_to_table())
            write_to_log(false);
#endif
        clock_t clk_val = write_to_log(true);
        thread_clock.merge(node_id, clk_val);
        return thread_clock;
    }

    inline void thread_acquire(const VectorClock &clock) {
        LOG_DEBUG("thread " << std::this_thread::get_id() << " acquire at " << this << std::dec << ", loc clock=" <<clock)
        thread_clock.merge(clock);
#ifdef USER_HELP_CONSUME
        help_consume(clock);
#else
        wait_for_consume(clock);
#endif
    }

    inline void log_store(char *addr) {
        uintptr_t cl_addr = (uintptr_t)addr & ~CL_UNIT_MASK;
        if (cl_addr == recent_cl)
            return;
        recent_cl = cl_addr;

        while (dirty_cls.insert((uintptr_t)addr))
            write_to_log(false);
#ifdef LOCAL_CL_TABLE_BUFFER
        if (dirty_cls.get_length_entry_count()!=0)
            write_to_log(false);
#endif
    }

//    inline void log_store_may_straddle(char *addr, size_t byte_offset) {
//        uintptr_t cl_addr = (uintptr_t)addr & ~CL_UNIT_MASK;
//        if (cl_addr == recent_cl)
//            return;
//        recent_cl = cl_addr;
//
//        while (dirty_cls.insert_may_straddle((uintptr_t)addr, byte_offset))
//            write_to_log(false);
//#ifdef LOCAL_CL_TABLE_BUFFER
//        if (dirty_cls.get_length_entry_count()!=0)
//            write_to_log(false);
//#endif
//    }

    inline void log_range_store(char *begin, char *end) {
        uintptr_t begin_addr = (uintptr_t)begin & ~CL_UNIT_MASK;
        uintptr_t end_addr = (uintptr_t)end & ~CL_UNIT_MASK;
        recent_cl = end_addr;

        while (dirty_cls.range_insert(begin_addr, end_addr))
            write_to_log(false);
        if (dirty_cls.get_length_entry_count() !=0)
            write_to_log(false);
    }
};

//TODO: use a non-thread-local node_id to access cache info when running multiple processes/machines, move these definitions to CacheInfo
extern thread_local ThreadOps *thread_ops;
extern CacheInfo *cache_infos;

inline bool check_range_invalidate(char *begin, char *end) {
        return cache_infos[thread_ops->get_node_id()].inv_cls.invalidate_range_if_dirty((uintptr_t)begin, (uintptr_t)end);
}

inline bool check_invalidate(char *addr) {
        return cache_infos[thread_ops->get_node_id()].inv_cls.invalidate_if_dirty((uintptr_t)addr);
}

} // RACoherence

#endif
