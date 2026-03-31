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
    uintptr_t recent_cl = 0;
    Log *curr_log = nullptr;

    inline void set_to_new_log(Log *& log) {
        while(!(log = log_mgrs[node_id].get_new_log())) {
            sched_yield();
        }
    }

    void write_cl_to_log(uintptr_t cl) {
        if (!curr_log)
            set_to_new_log(curr_log);
        if (curr_log->is_full()) {
            log_mgrs[node_id].produce_tail(curr_log, false);
            set_to_new_log(curr_log);
        }
        curr_log->write(cl);
    }

    clock_t write_to_log(bool is_release) {
        using namespace cl_group;
        clock_t clk_val = 0;
#if DELAY_PUBLISH
        if (!curr_log)
            set_to_new_log(curr_log);
#else
        Log *curr_log;
        set_to_new_log(curr_log);
#endif

        for(auto entry: dirty_cls) {
            if (!entry)
                continue;
#if DELAY_PUBLISH
            if (curr_log->is_full()) {
                clk_val = log_mgrs[node_id].produce_tail(curr_log, is_release);
                STATS(cache_info->produced_count++;)
                LOG_DEBUG("node " << node_id << " produce log " << cache_info->produced_count)
                set_to_new_log(curr_log);
            }
#endif
            curr_log->write(entry);
#if !EAGER_WRITEBACK
            if (is_length_based(entry)) {
                for (auto cl_addr: LengthCLRange(entry))
                    for (unsigned i = 0; i < GROUP_SIZE * CL_EXPAND_FACTOR; i++)
                        do_writeback((char *)cl_addr + (i * CACHE_LINE_SIZE));
            } else {
                for (auto cl_addr: MaskCLRange(get_ptr(entry), get_mask16(entry)))
                    for (unsigned i = 0; i < CL_EXPAND_FACTOR; i++)
                        do_writeback((char *)cl_addr + i * CACHE_LINE_SIZE);
            }
#endif
        }

        // release store in LogManager::produce_tail acts as writeback fence
#if DELAY_PUBLISH
        if (is_release) {
             clk_val = log_mgrs[node_id].produce_tail(curr_log, is_release);
             curr_log = nullptr;
        }
#else
        clk_val = log_mgrs[node_id].produce_tail(curr_log, is_release);
#endif
        STATS(cache_info->produced_count++;)
        LOG_DEBUG("node " << node_id << " produce log " << cache_info->produced_count)
        dirty_cls.clear_table();
        return clk_val;
    }

public:
    ThreadOps() = default;
    ThreadOps(LogManager *lmgrs, CacheInfo *cinfo, unsigned nid, unsigned tid): log_mgrs(lmgrs), cache_info(cinfo), node_id(nid), thread_id(tid) {}
    ThreadOps &operator=(const ThreadOps &other) = default;

    unsigned get_node_id() { return node_id; }
    unsigned get_thread_id() { return thread_id; }
    const VectorClock &get_clock() {return thread_clock; }

    inline void help_consume(const VectorClock &target) {
        bool done = false;
        bool node_done[NODE_COUNT] = {false};
        while (!done) {
            done = true;
            for (unsigned i=NODE_COUNT; i-- > 0;) {
                if (node_done[i] || i == node_id)
                    continue;

                if (cache_info->get_clock(i) >= target[i]) {
                    node_done[i] = true;
                    continue;
                }

                if (!log_mgrs[i].is_subscribed(node_id)) {
                    node_done[i] = true;
                    continue;
                }

                CLHMutex &mtx = log_mgrs[i].get_head_mutex(node_id);
                if (!mtx.try_lock()) {
                    done = false;
                    continue;
                }

                auto clk = cache_info->get_clock(i);
                while(clk < target[i]) {
                    const PubEntry* entry;
                    //entry might be null because of logs yet to be produced before the target log
                    while(!(entry = log_mgrs[i].take_head(node_id)));
                    Log *log = entry->log.load(std::memory_order_relaxed);
                    if (entry->is_rel)
                        clk = entry->idx.load(std::memory_order_relaxed);
                    cache_info->process_log(*log);
                    log_mgrs[i].consume_head(node_id);
                    STATS(cache_info->consumed_count[i]++;)
                    LOG_DEBUG("node " << node_id << " consume log " << cache_info->consumed_count[i] << " from " << i)
                }
                node_done[i] = true;
                cache_info->update_clock(i, clk);
                mtx.unlock();
                // mutex unlock takes care of invalidate fence
            }
        }
    }

    inline void wait_for_consume(const VectorClock &target) {
       for (unsigned i = 0; i<NODE_COUNT; i++) {
           if (i == node_id)
               continue;
           if (!log_mgrs[i].is_subscribed(node_id))
               continue;
           while (cache_info->get_clock(i) < target[i]) {
               LOG_DEBUG("node " << node_id << " block on acquire, index=" << i << ", target=" << target[i] << ", current=" << curr)
               sched_yield();
           }
       }
    }

    inline bool thread_release() {
        LOG_DEBUG("thread " << std::this_thread::get_id() << " release at " << this << std::dec << ", thread clock=" <<thread_clock)
        if (!recent_cl)
            return false;

#if EAGER_WRITEBACK
        uintptr_t recent_addr = recent_cl << VIRTUAL_CL_SHIFT;
        for (unsigned i = 0; i < CL_EXPAND_FACTOR; i++)
             do_writeback((char *)recent_addr + i * CACHE_LINE_SIZE);
#endif

        recent_cl = 0;

#ifdef LOCAL_CL_TABLE_BUFFER
        while (dirty_cls.dump_buffer_to_table())
            write_to_log(false);
#endif

#if IMMEDIATE_PUBLISH
        clock_t clk_val = log_mgrs[node_id].produce_tail(curr_log, true);
        curr_log = nullptr;
#else
        clock_t clk_val = write_to_log(true);
#endif
        //increment
        thread_clock.assign(node_id, clk_val);
        return true;
    }

    inline void thread_acquire(const VectorClock &clock) {
        LOG_DEBUG("thread " << std::this_thread::get_id() << " acquire at " << this << std::dec << ", loc clock=" <<clock)
#if CONSUME_HELPING
        help_consume(clock);
#else
        wait_for_consume(clock);
#endif
        thread_clock.merge(clock);
    }

    inline void log_store(char *addr) {
        uintptr_t cl = (uintptr_t)addr >> VIRTUAL_CL_SHIFT;
#if INLINE_CACHING
        if (cl == recent_cl)
            return;
#endif
#if EAGER_WRITEBACK
        if (recent_cl) {
            uintptr_t recent_addr = recent_cl << VIRTUAL_CL_SHIFT;
            for (unsigned i = 0; i < CL_EXPAND_FACTOR; i++)
                 do_writeback((char *)recent_addr + i * CACHE_LINE_SIZE);
        }
#endif
        recent_cl = cl;

#if IMMEDIATE_PUBLISH
        write_cl_to_log(cl);
#else
        if (dirty_cls.insert((uintptr_t)cl)) {
            write_to_log(false);
            dirty_cls.insert((uintptr_t)cl);
        }
#ifdef LOCAL_CL_TABLE_BUFFER
        if (dirty_cls.get_length_entry_count()!=0)
            write_to_log(false);
#endif
#endif
    }

    inline void log_range_store(char *begin, char *end) {
        // EAGER_WRITEBACK not implemented here for efficiency, need to be handled by caller
        uintptr_t begin_addr = (uintptr_t)begin >> VIRTUAL_CL_SHIFT;
        uintptr_t end_addr = (uintptr_t)end >> VIRTUAL_CL_SHIFT;
        recent_cl = end_addr;
#if IMMEDIATE_PUBLISH
        for (uintptr_t cl = begin_addr; cl < end_addr; cl++)
            write_cl_to_log(cl);
#else
        while (dirty_cls.range_insert(begin_addr, end_addr))
            write_to_log(false);
#ifdef LOCAL_CL_TABLE_BUFFER
        if (dirty_cls.get_length_entry_count() !=0)
            write_to_log(false);
#endif
#endif
    }
};

//TODO: use a non-thread-local node_id to access cache info when running multiple processes/machines, move these definitions to CacheInfo
extern __thread ThreadOps *thread_ops;
extern CacheInfo *cache_infos;

inline bool check_range_invalidate(char *begin, char *end) {
        return cache_infos[thread_ops->get_node_id()].inv_cls.invalidate_range_if_dirty((uintptr_t)begin, (uintptr_t)end);
}

inline bool check_invalidate(char *addr) {
        return cache_infos[thread_ops->get_node_id()].inv_cls.invalidate_if_dirty((uintptr_t)addr);
}

} // RACoherence

#endif
