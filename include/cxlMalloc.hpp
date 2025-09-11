#ifndef _CXL_MALLOC_H_
#define _CXL_MALLOC_H_
#include <stddef.h>

#if __cplusplus
extern "C" {
#endif

void cxlnhc_pool_initialize(char *hc_buf, char *buf, size_t size);

void *cxlnhc_malloc(size_t size);

void cxlnhc_free(void *ptr, size_t size);

void cxlhc_pool_initialize(char *buf, size_t size);

void *cxlhc_malloc(size_t size);

void cxlhc_free(void *ptr, size_t size);

#if __cplusplus
}
#endif

#if __cplusplus
#include <cassert>

template <typename T> 
class cxlhc_allocator { 
public:// type definitions
	typedef T value_type;
	typedef T*       pointer;
	typedef const T* const_pointer;
	typedef T&       reference;
	typedef const T& const_reference;
	typedef size_t size_type;
	typedef size_t difference_type;

	// rebind allocator to type U
	template <class U>
	struct rebind {
		typedef cxlhc_allocator<U> other;
	};

	// return address of values
	pointer address(reference value) const {
		return &value;
	}
	const_pointer address(const_reference value) const {
		return &value;
	}

    //constructors and destructors
    cxlhc_allocator() throw() {}

	cxlhc_allocator(const cxlhc_allocator&) throw() {}

	template<typename T2>
    cxlhc_allocator(const cxlhc_allocator<T2> &alloc) throw() {}
 
    ~cxlhc_allocator() throw() {}

	//operators
	bool operator!=(const cxlhc_allocator<T> other) {return true;} 

	// Allocate memory for n objects of type T 
    pointer allocate(size_t n) {
        //mspace_malloc_stats(cxlhc_space);
        void *addr = cxlhc_malloc(n * sizeof(T));
        if (!addr)
            assert(false && "bad alloc");
        return static_cast<pointer>(addr);
    }

	template<typename... _Args>
	// initialize elements of allocated storage p with value value
	void construct(pointer p, _Args&&... args) {
		// initialize memory with placement new
		new((void*)p)T(static_cast<_Args&&>(args)...);
	}

	// destroy elements of initialized storage p
	void destroy(pointer p) {
		// destroy objects by calling their destructor
		p->~T();
	}
    // Deallocate memory 
    void deallocate(pointer p, size_t n) {
        cxlhc_free(p, n);
    }
};
#endif


#endif
