#ifndef _MALLOC_H_
#define _MALLOC_H_

#include <cstddef>

extern "C" {
    /* MSPACE */
    /*
      mspace is an opaque type representing an independent
      region of space that supports mspace_malloc, etc.
    */
    
    typedef void * mspace;
    
    extern void * mspace_malloc(mspace msp, size_t bytes);
    extern void mspace_free(mspace msp, void* mem);
    extern void * mspace_realloc(mspace msp, void* mem, size_t newsize);
    extern void * mspace_calloc(mspace msp, size_t n_elements, size_t elem_size);
    extern void * mspace_memalign(mspace msp, size_t alignment, size_t bytes);
    extern mspace create_mspace_with_base(void* base, size_t capacity, int locked);
    extern mspace create_mspace(size_t capacity, int locked);
    extern void mspace_malloc_stats(mspace msp);
}
#endif
