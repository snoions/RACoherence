#ifndef _USER_H_
#define _USER_H_

#include "cxlMalloc.hpp"
#include "stdint.h"
#include "pthread.h"
#include "config.hpp"

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

int rac_thread_create(unsigned nid, pthread_t *thread, void *(*func)(void*), void *arg);

int rac_thread_join(unsigned nid, pthread_t thread, void **thread_ret);

void rac_init(unsigned nid, size_t cxl_hc_range, size_t cxl_nhc_range);

void rac_shutdown();

unsigned rac_get_node_id();

#if __cplusplus
}
#endif

#endif
