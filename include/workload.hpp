#ifndef _WORKLOAD_H_
#define _WORKLOAD_H_

#include "config.hpp"

enum OpType {
    OP_STORE,
    OP_STORE_REL,
    OP_LOAD,
    OP_LOAD_ACQ,
    OP_END
};

struct UserOp {
    OpType op;
    size_t offset;
};

class SeqWorkLoad {
    virt_addr_t range;
    unsigned align;
    virt_addr_t atomic_range;
    unsigned atomic_align;
    unsigned plain_acq_rel_ratio;

public:
    SeqWorkLoad(virt_addr_t rg, unsigned al, virt_addr_t atomic_rg, unsigned atomic_al, unsigned par_ratio): range(rg), align(al), atomic_range(atomic_rg), atomic_align(atomic_al), plain_acq_rel_ratio(par_ratio) {}
    
    inline UserOp getNextOp(unsigned index) {
        //assume all atomic is acquire release for now
        bool is_acq_rel = (index % plain_acq_rel_ratio) == 0;
        OpType op;
        if ((index % SEQ_OP_FACTOR) % 2 == 0)
            op = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            op = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t offset = is_acq_rel ? 
            (atomic_align * index ) % atomic_range:
            (align * index) % range;
        return {op, offset};
    }
};


// xorshf96
inline unsigned long fast_rand() {          //period 2^96-1
    static unsigned long x=123456789, y=362436069, z=521288629;
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}


// May not be data-race free
class RandWorkLoad {
    virt_addr_t range;
    unsigned align;
    virt_addr_t atomic_range;
    unsigned atomic_align;
    unsigned plain_acq_rel_ratio;

public:
    RandWorkLoad(virt_addr_t rg, unsigned al, virt_addr_t atomic_rg, unsigned atomic_al, unsigned par_ratio): range(rg), align(al), atomic_range(atomic_rg), atomic_align(atomic_al), plain_acq_rel_ratio(par_ratio) {}
    
    inline UserOp getNextOp(unsigned index) {
        bool is_acq_rel = (fast_rand() % plain_acq_rel_ratio) == 0;
        OpType op;
        if ((fast_rand() % 2) == 0)
            op = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            op = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t offset = is_acq_rel ? 
            (fast_rand() % atomic_range)/ atomic_align * atomic_align:
            (fast_rand() % range)/align * align;
        return {op, offset};
    }
};
#endif
