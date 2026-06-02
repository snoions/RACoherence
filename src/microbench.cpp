//#include <iostream>

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

//TODO: refactor into multiple variants
void Microbench::run() {
    //int unsubbed = -1;
    //int node_id = rac_get_node_id();
    for (int i =0; i < TOTAL_OPS; i++) {
        //TODO: use barrier to make sure consume helping does not conflict with (un)subscribing
        //if (local_thread_id == 0 && i % (TOTAL_OPS/10) == 0) {
        //    if (unsubbed != -1) {
        //        rac_subscribe_to_node(unsubbed);
        //        std::cout << node_id << " subscribe to " << unsubbed << std::endl;
        //    }
        //    unsubbed = (unsubbed+1)%NODE_COUNT;
        //    unsigned node_id = rac_get_node_id();
        //    if (unsubbed == node_id)
        //        unsubbed = (unsubbed+1)%NODE_COUNT;
        //    rac_unsubscribe_from_node(unsubbed);
        //    std::cout << node_id << " unsubscribe from " << unsubbed << std::endl;
        //}

        UserOp op = workload.getNextOp(i);
#ifdef WORKLOAD_USE_LOCKS
        use_locks(op);
#endif
        //TODO: data-race-free workload based on synchronization (locked region?)
        switch (op.type) {
            case OP_STORE_RLS: {
                cxl_pool.atomic_data[op.offset].store(0, std::memory_order_release);
                break;
            }
             case OP_STORE: {
                rac_store8(&cxl_pool.data[op.offset], 0, nullptr);
                // to simulate cache line alternations
                // size_t next_off = op.offset+64 > CXL_POOL_DATA_SIZE? op.offset: op.offset+64;
                // rac_store8(&cxl_pool.data[next_off] , 0, nullptr);
                // rac_store8(&cxl_pool.data[op.offset], 0, nullptr);
                break;
            }
            case OP_LOAD_ACQ: {
                cxl_pool.atomic_data[op.offset].load(std::memory_order_acquire);
                break;
            }
            case OP_LOAD: {
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
