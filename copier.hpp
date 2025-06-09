#ifndef _COPIER_H_
#define _COPIER_H_

#include <unordered_set>
#include <iostream>

#include "types.hpp"
#include "config.hpp"
#include "logBuffer.hpp"

extern thread_local unsigned node_id;

class Copier {
    epoch_t tails[NODECOUNT] = {0};
    std::unordered_set<uintptr_t> invalid_dir;
    LogBuffer *buffers;

public:
    Copier(LogBuffer *bufs): buffers(bufs) {}

    void run() {
        unsigned count = 0;
        while(count < EPOCH) {
            for (int i=0; i<NODECOUNT; i++) {
                if (i == node_id)
                    continue;
                auto tail_log = buffers[i].consumeTail(tails[i]);
                if (!tail_log)
                    continue;
                for (auto invalid_cl: *tail_log) {
                    invalid_dir.insert(invalid_cl);
                }
                std::cout << "node " << node_id << ": consume log " << count++ << " of " << i << std::endl;
            }
        }
    }
};

#endif
