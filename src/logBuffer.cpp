#include "logBuffer.hpp"

void Log::consume() {
    auto cp = SPair(status.fetch_sub(1, std::memory_order_relaxed));
    assert(produced(cp.c)); 
}

void Log::produce(bool is_r) {
    is_rel = is_r;
    auto cp = SPair(status.fetch_sub(1, std::memory_order_release));
    assert(cp.c==NODE_COUNT);
}

//could also use a tail lock instead, performance seems similar
Log *LogBuffer::take_tail() {
    auto t = tail.load(std::memory_order_acquire);
    Log &log = log_from_index(t.idx);
    if (!log.prepare_produce(t.par))
        return NULL;
    t.next();
    tail.store(t,std::memory_order_release);
    return &log;
}

Log &LogBuffer::take_head(unsigned nid) {
    Log &log = log_from_index(heads[nid].idx);
    log.prepare_consume(heads[nid].par);
    heads[nid].next();
    return log;
}


Log *LogBuffer::try_take_head(unsigned nid) {
    Log &log = log_from_index(heads[nid].idx);
    if (!log.try_prepare_consume(heads[nid].par))
        return NULL;
    heads[nid].next();
    return &log;
}
