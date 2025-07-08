#ifndef _WORKLOAD_H_
#define _WORKLOAD_H_

#include <random>

#include "config.hpp"

static unsigned long x=123456789, y=362436069, z=521288629;

unsigned long xorshf96(void) {          //period 2^96-1
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
        if (xorshf96() % 2 == 0)
            op = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            op = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t offset = is_acq_rel ? 
            (align * index) % range: 
            (atomic_align * index ) % atomic_range;
        return {op, offset};
    }
};

//random values have high overheads, also may not be data-race free
class RandWorkLoad {
    virt_addr_t range;
    unsigned align;
    virt_addr_t atomic_range;
    unsigned atomic_align;
    unsigned plain_acq_rel_ratio;

public:
    RandWorkLoad(virt_addr_t rg, unsigned al, virt_addr_t atomic_rg, unsigned atomic_al, unsigned par_ratio): range(rg), align(al), atomic_range(atomic_rg), atomic_align(atomic_al), plain_acq_rel_ratio(par_ratio) 
    {
        //srand(121);
    }
    
    inline UserOp getNextOp(unsigned index) {
        bool is_acq_rel = (rand() % plain_acq_rel_ratio) == 0;
        OpType op;
        if ((rand() % 2) == 0)
            op = is_acq_rel ? OP_LOAD_ACQ: OP_LOAD;
        else
            op = is_acq_rel ? OP_STORE_REL: OP_STORE;
        size_t offset = is_acq_rel ? 
            (rand() % range)/align * align: 
            (rand() % atomic_range)/ atomic_align * atomic_align;
        return {op, offset};
    }
};
#endif
