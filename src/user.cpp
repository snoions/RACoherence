#include "user.hpp"
#include "workload.hpp"
#include "logger.hpp"

template <typename W>
void User::run(W &workload) {
    for (int i =0; i < TOTAL_OPS; i++) {
        UserOp op = workload.getNextOp(i);
        //TODO: data-race-free workload based on synchronization (locked region?)
        switch (op.type) {
            case OP_STORE_REL:
#ifdef STATS
                writ_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_store_release_raw(&cxl_pool.data[op.offset], 0);
#else
                //handle_store_release(&cxl_pool.data[op.offset], 0);
                cxl_pool.atomic_data[op.offset].store(0, std::memory_order_release);
#endif
            case OP_STORE: {
#ifdef STATS
                write_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_store_raw(&cxl_pool.data[op.offset], 0);
#else
                handle_store(&cxl_pool.data[op.offset], 0);
#endif
                break;
            } 
            case OP_LOAD_ACQ: {
#ifdef STATS
                read_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_load_acquire_raw(&cxl_pool.data[op.offset]);
#else
                //handle_load_acquire(&cxl_pool.data[op.offset]);
                cxl_pool.atomic_data[op.offset].load(std::memory_order_acquire);
#endif
                break;
            }
            case OP_LOAD: {
#ifdef STATS
                read_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_load_raw(&cxl_pool.data[op.offset]);
#else
                handle_load(&cxl_pool.data[op.offset]);
#endif
                break;
            }
            default:
                assert("unreachable");
        }
    }
    LOG_INFO("node " << node_id << " user " << user_id << " done")
}

template void User::run<RandWorkLoad>(RandWorkLoad &workload);
template void User::run<SeqWorkLoad>(SeqWorkLoad &workload);

