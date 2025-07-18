#include "user.hpp"
#include "workload.hpp"
#include "logger.hpp"

//should support taking multiple heads for more flexibility
void User::write_to_log(bool is_release) {
    Log *curr_log;
    while(!(curr_log = my_buf().take_tail())) {
#ifdef STATS
        blocked_count++;
#endif
        //wait for available space
        sleep(0);
    }

    for(auto cl: dirty_cls) {
        if (cl) {
            curr_log->write(cl);
            for(auto addr : MaskedPtrRange(cl))
                do_flush((char *)addr);
        }
    }
    flush_fence();
    curr_log->produce(is_release); 
    dirty_cls.clear();
}

void User::user_help_consume(const VectorClock &target) {
    for (unsigned i=0; i<NODE_COUNT; i++) {
        if (i == node_id)
            continue;
        auto val = cache_info.get_clock(i);
        if (val >= target[i])
            continue;

        std::unique_lock<std::mutex> l(cxl_meta.bufs[i].get_head_mutex(node_id));
        //check again after wake up
        val = cache_info.get_clock(i);

        while(val < target[i]) {
            Log &log = cxl_meta.bufs[i].take_head(node_id);
            cache_info.process_log(log);
            if (log.is_release())
                val = cache_info.update_clock(i);
            log.consume();
            LOG_INFO("node " << node_id << " consume log " << ++cache_info.consumed_count << " from " << i);
        }

    }
}

void User::wait_for_consume(const VectorClock &target) {
    while(true) {
        bool uptodate = true;
        for (unsigned i=0; i<NODE_COUNT; i++) {
            auto curr = cache_info.get_clock(i);
            if (i != node_id && curr < target[i]) {
                uptodate = false;
                LOG_DEBUG("block on acquire, index=" << i << ", target=" << target[i] << ", current=" << curr);
                break;
            }
        }
        if (uptodate)
            break;
        sleep(0);
    }
}

void User::handle_store(char *addr, bool is_release) {
    uintptr_t cl_addr = (uintptr_t)addr & CACHE_LINE_MASK;

    bool full = dirty_cls.insert(cl_addr);
    //write to log either on release store or on reaching log size limit
    //if(is_release || dirty_cls.size() == LOG_SIZE) {
    if(is_release || full) {
        write_to_log(is_release);
        LOG_INFO("node " << node_id << " produce log " << cache_info.produced_count++);
    }

    if(is_release) {
       const auto &clock = user_clock.mod([=] (auto &self) {
           self.tick(node_id);
           return self;
       });
       LOG_INFO("node " << node_id << " release at " << (void *)addr << std::dec << ", clock=" << clock);
       size_t off = addr - cxl_data;
       cxl_meta.atmap[off].mod([&](auto &self) {
           self.clock.merge(clock);
       });

       ((volatile std::atomic<char> *)addr)->store(0, std::memory_order_release);
    } else {
        if (cache_info.invalidate_if_dirty(addr)) {
#ifdef STATS
            invalidate_count++;
#endif
        }
        *((volatile char *)addr) = 0;
    }
}

char User::handle_load(char *addr, bool is_acquire) {
        if (cache_info.invalidate_if_dirty(addr)) {
#ifdef STATS
            invalidate_count++;
#endif
        }

    char ret;
    if (is_acquire) {
        size_t off = addr - cxl_data;
        auto at_clk = cxl_meta.atmap[off].get([&](auto &self) { 
            ret = ((volatile std::atomic<char> *)addr)->load(std::memory_order_acquire);
            return self.clock; 
        });

        LOG_INFO("node " << node_id << " acquire " << (void*) addr << std::dec << ", clock=" << at_clk);

#ifdef USER_HELP_CONSUME
        user_help_consume(at_clk);
#else
        wait_for_consume(at_clk);
#endif

        user_clock.mod([&](auto &self) {
            self.merge(at_clk);
        });
    } else 
        ret = *((volatile char *)addr);

    return ret;
}

template <typename W>
void User::run(W &workload) {
    for (int i =0; i < TOTAL_OPS; i++) {
        UserOp op = workload.getNextOp(i);
        //TODO: data-race-free workload based on synchronization (locked region?)
        switch (op.type) {
            case OP_STORE_REL:
#ifdef STATS
                write_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_store_raw(&cxl_data[op.offset], true);
#else
                handle_store(&cxl_data[op.offset], true);
#endif
            case OP_STORE: {
#ifdef STATS
                write_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_store_raw(&cxl_data[op.offset]);
#else
                handle_store(&cxl_data[op.offset]);
#endif
                break;
            } 
            case OP_LOAD_ACQ: {
#ifdef STATS
                read_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_load_raw(&cxl_data[op.offset], true);
#else
                handle_load(&cxl_data[op.offset], true);
#endif
                break;
            }
            case OP_LOAD: {
#ifdef STATS
                read_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_load_raw(&cxl_data[op.offset]);
#else
                handle_load(&cxl_data[op.offset]);
#endif
                break;
            }
            default:
                assert("unreachable");
        }
    }
}

template void User::run<RandWorkLoad>(RandWorkLoad &workload);
template void User::run<SeqWorkLoad>(SeqWorkLoad &workload);

