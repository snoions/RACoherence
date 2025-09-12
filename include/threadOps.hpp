#ifndef _THREAD_OPS_H_
#define _THREAD_OPS_H_

#include <unistd.h>

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
    unsigned thread_id;

    //thread local data
    VectorClock thread_clock;
    LocalCLTable dirty_cls;
    //maybe should support taking multiple heads for more flexibility
    clock_t write_to_log(bool is_release) {
        Log *curr_log;
        while(!(curr_log = log_mgrs[node_id].get_new_log())) {
            STATS(blocked_count++)
            sleep(0);
        }

        struct FlushOp {
            inline void operator() (uintptr_t ptr, uint64_t mask) {
                for (auto cl_addr: MaskCLRange(ptr, mask))
                    do_flush((char *)cl_addr);
            }
        };

        for(auto cg: dirty_cls) {
            if (cg) {
                curr_log->write(cg);
                process_cl_group(cg, FlushOp());
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
            sleep(0);
        }
    }

public:
    ThreadOps() = default;
    ThreadOps(LogManager *lmgrs, CacheInfo *cinfo, unsigned nid, unsigned tid): log_mgrs(lmgrs), cache_info(cinfo), node_id(nid), thread_id(tid) {}
    ThreadOps &operator=(const ThreadOps &other) = default;

    unsigned get_node_id() { return node_id; }
    unsigned get_thread_id() { return thread_id; }
    const VectorClock &get_clock() {return thread_clock; }
    void merge_clock(const VectorClock &other) { thread_clock.merge(other); }

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

    inline void log_store(char *addr) {
    //    uintptr_t cl_addr = (uintptr_t)addr & ~CACHE_LINE_MASK;

    //    while (dirty_cls.insert(cl_addr) || dirty_cls.get_length_entry_count() != 0)
    //        write_to_log(false);
    }

    inline void log_range_store(char *begin, char *end) {
        uintptr_t begin_addr = (uintptr_t)begin & ~CACHE_LINE_MASK;
        uintptr_t end_addr = (uintptr_t)end & ~CACHE_LINE_MASK;

        while (dirty_cls.range_insert(begin_addr, end_addr) || dirty_cls.get_length_entry_count() != 0)
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
#endif
