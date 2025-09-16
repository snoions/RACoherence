/******************************************************************************
 * Copyright (c) 2014, Pedro Ramalhete, Andreia Correia
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Concurrency Freaks nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************
 */
#ifndef _CLH_MUTEX_H_
#define _CLH_MUTEX_H_

#include <thread>
#include <atomic>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <sched.h>

namespace RACoherence {

typedef struct clh_mutex_node_ clh_mutex_node_t;

struct clh_mutex_node_
{
    std::atomic<char> succ_must_wait;
    std::thread::id id;
};

typedef struct
{
    clh_mutex_node_t * mynode;
    char padding[64];  // To avoid false sharing with the tail
    std::atomic<clh_mutex_node_t *> tail;
} clh_mutex_t;


void clh_mutex_init(clh_mutex_t * self);
void clh_mutex_destroy(clh_mutex_t * self);
void clh_mutex_lock(clh_mutex_t * self);
void clh_mutex_unlock(clh_mutex_t * self);

struct CLHMutex: private clh_mutex_t {
    CLHMutex() {
        clh_mutex_init(this);
    }

    ~CLHMutex() {
        clh_mutex_destroy(this);
    }

    void lock() {
        clh_mutex_lock(this);
    }

    void unlock() {
        clh_mutex_unlock(this);
    }

};

} // RACoherence

#endif /* _CLH_MUTEX_H_ */
