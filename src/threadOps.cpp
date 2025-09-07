#include <unistd.h>

#include "threadOps.hpp"
#include "logger.hpp"

//maybe should support taking multiple heads for more flexibility
clock_t ThreadOps::write_to_log(bool is_release) {
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

void ThreadOps::help_consume(const VectorClock &target) {
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

void ThreadOps::wait_for_consume(const VectorClock &target) {
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
