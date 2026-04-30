#ifndef _CXL_MALLOC_H_
#define _CXL_MALLOC_H_
#include <stddef.h>
#include "extentPool.hpp"

#if __cplusplus
extern "C" {
#endif

void *cxlnhc_malloc(size_t size);

void *cxlnhc_cl_aligned_malloc(size_t size);

void cxlnhc_free(void *ptr, size_t size);

void *cxlhc_malloc(size_t size);

void cxlhc_free(void *ptr, size_t size);

#if __cplusplus
}
#endif

#if __cplusplus
#include <cassert>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace RACoherence {

struct AllocMeta {
#ifndef USE_MIMALLOC
    ExtentPool cxlhc_pool;
    ExtentPool cxlnhc_pool;
#endif
};

#ifndef USE_MIMALLOC
void print_jemalloc_stats();
#endif

void cxl_alloc_process_init(AllocMeta *a_meta, char *hc_buf, size_t hc_range, char *nhc_buf, size_t nhc_range, bool is_first);

void cxl_alloc_thread_init();

void cxl_alloc_thread_exit();

template <typename T> 
class CXLHCAllocator {
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
		typedef CXLHCAllocator<U> other;
	};

	// return address of values
	pointer address(reference value) const {
		return &value;
	}
	const_pointer address(const_reference value) const {
		return &value;
	}

    //constructors and destructors
    CXLHCAllocator() throw() {}

	CXLHCAllocator(const CXLHCAllocator&) throw() {}

	template<typename T2>
    CXLHCAllocator(const CXLHCAllocator<T2> &/*alloc*/) throw() {}
 
    ~CXLHCAllocator() throw() {}

	//operators
	bool operator!=(const CXLHCAllocator<T> /*other*/) {return true;}

	// Allocate memory for n objects of type T 
    pointer allocate(size_t n) {
        void *addr = cxlhc_malloc(n * sizeof(T));
        if (!addr) {
            std::bad_alloc exception;
            throw exception;
        }
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
        cxlhc_free(p, n * sizeof(T));
    }
};

template<typename K, typename T>
using cxlhc_map = std::map<K, T, std::less<K>, CXLHCAllocator<std::pair<const K, T>>> ;
template<typename T>
using cxlhc_set = std::set<T, std::less<T>, CXLHCAllocator<T>> ;
template<typename K, typename T>
using cxlhc_unordered_map = std::unordered_map<K, T, std::hash<K>,  std::equal_to<K>, CXLHCAllocator<std::pair<const K, T>>> ;
template<typename T>
using cxlhc_vector = std::vector<T, CXLHCAllocator<T>>;

} // RACoherence

#endif
#endif
