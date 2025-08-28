#include "user.hpp"
#include "workload.hpp"
#include "logger.hpp"

void User::run() {
    WORKLOAD_TYPE workload;

    for (int i =0; i < TOTAL_OPS; i++) {
        UserOp op = workload.getNextOp(i);
        //TODO: data-race-free workload based on synchronization (locked region?)
        switch (op.type) {
            case OP_STORE_RLS: {
#ifdef STATS
                write_count++;
#endif
#ifdef PROTOCOL_OFF
                handle_store_release_raw(&cxl_pool.data[op.offset], 0);
#else
                cxl_pool.atomic_data[op.offset].store(0, std::memory_order_release);
#endif
                break;
            }
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
            case OP_LOCK: {
                cxl_pool.mutexes[op.offset].lock();
                break;
            }
            case OP_UNLOCK: {
                cxl_pool.mutexes[op.offset].unlock();
                break;
            }
            default:
                assert("unreachable");
        }
    }
    LOG_INFO("node " << node_id << " user " << user_id << " done")
}
