#ifndef _COPIER_H_
#define _COPIER_H_

#include <unordered_set>
#include <iostream>

#include "unistd.h"

#include "types.hpp"
#include "config.hpp"
#include "logBuffer.hpp"

extern thread_local unsigned node_id;

class Copier {
    epoch_t tail_epochs[NODECOUNT] = {0};
    std::unordered_set<uintptr_t> invalid_dir;
    LogBuffer *buffers;

public:
    Copier(LogBuffer *bufs): buffers(bufs) {}

    void run() {
        unsigned count = 0;
        while(count < EPOCH * WORKER_PER_NODE) {
            for (int i=0; i<NODECOUNT; i++) {
                if (i == node_id)
                    continue;
                Log* tail = buffers[i].consumeTail(tail_epochs[i]);
                if (!tail)
                    continue;
                for (auto invalid_cl: *tail) {
                    invalid_dir.insert(invalid_cl);
                }
                tail_epochs[i]++;
                std::cout << "node " << node_id << ": consume log " << count++ << " of " << i << std::endl;
            }
        }
    }
};

#endif
