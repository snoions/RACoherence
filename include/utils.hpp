#ifndef _UTIL_H_
#define _UTIL_H_

#include <array>
#include <shared_mutex>

#include "config.hpp" 

template<typename T>
using PerNode = std::array<T, NODE_COUNT>;

template<typename T>
struct alignas(CACHE_LINE_SIZE) CacheAligned: public T {
    using T::T;
};

template<typename T>
T &lvalue(T &&t)
{
    return t;
}

template<typename T>
class Monitor
{
    std::shared_mutex m;
    T t;
public:
    Monitor() = default;
    Monitor(T &&arg_t): t(std::forward<T>(arg_t)) {}

    template<typename F>
    auto get(F f) ->decltype(f(t)) const
    {
        return std::shared_lock<std::shared_mutex>(m),
               f(t);
    }
 
    template<typename F>
    auto mod(F f) ->decltype(f(t))
    {
        return std::unique_lock<std::shared_mutex>(m),
               f(t);
    }

    T& get_raw()
    {
        return t;
    }
 
    const T& get_raw() const
    {
        return t;
    }
};

#endif
