#include "microbench.hpp"
#include "runtime.hpp"
#include "logger.hpp"

namespace RACoherence {

inline void Microbench::use_locks(UserOp &op) {
    if (op.type != OP_STORE_RLS && op.type != OP_LOAD_ACQ)
        return;
    if (locked_offset == CXL_SYNC_RANGE) {
        op.type = OP_LOCK;
        locked_offset =op.offset;
    } else {
        op.type = OP_UNLOCK;
        op.offset = locked_offset;
        locked_offset = CXL_SYNC_RANGE;
    }
}

void Microbench::run() {
    for (int i =0; i < TOTAL_OPS; i++) {
        UserOp op = workload.getNextOp(i);
#ifdef WORKLOAD_USE_LOCKS
        use_locks(op);
#endif
        //TODO: data-race-free workload based on synchronization (locked region?)
        switch (op.type) {
            case OP_STORE_RLS: {
                STATS(write_count++)
                cxl_pool.atomic_data[op.offset].store(0, std::memory_order_release);
                break;
            }
             case OP_STORE: {
                STATS(write_count++)
                rac_store8(&cxl_pool.data[op.offset], 0, nullptr);
                break;
            }
            case OP_LOAD_ACQ: {
                STATS(read_count++)
                cxl_pool.atomic_data[op.offset].load(std::memory_order_acquire);
                break;
            }
            case OP_LOAD: {
                STATS(read_count++)
                rac_load8(&cxl_pool.data[op.offset], nullptr);
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

    if(locked_offset != CXL_SYNC_RANGE)
        cxl_pool.mutexes[locked_offset].unlock();
    LOG_INFO("node " << node_id << " thread " << thread_id << " done")
}

} // RACoherence
