#ifndef _USER_H_
#define _USER_H_

#include "threadOps.hpp"

extern thread_local ThreadOps *thread_ops;

#if __cplusplus
extern "C" {
#endif
uint8_t rac_load8(void *addr, const char *);
uint16_t rac_load16(void *addr, const char *);
uint32_t rac_load32(void *addr, const char *);
uint64_t rac_load64(void *addr, const char *);

void rac_store8(void * addr, uint8_t val, const char *);
void rac_store16(void * addr, uint16_t val, const char *);
void rac_store32(void * addr, uint32_t val, const char *);
void rac_store64(void * addr, uint64_t val, const char *);

#if __cplusplus
}
#endif

typedef struct RACThreadArgs_ {
    unsigned nid;
    std::function<void*(void*)>func;
    void* arg;
} RACThreadArgs;

void *rac_thread_func_wrapper(void *arg);

void rac_init(unsigned nid);

void rac_shutdown();

#endif
