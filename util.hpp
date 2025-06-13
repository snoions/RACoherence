#ifndef _UTIL_H_
#define _UTIL_H_

#include <array>
#include <mutex>

#include "config.hpp" 

template<typename T>
using PerNodeData = std::array<T, NODECOUNT>;

template<typename T>
T &lvalue(T &&t)
{
    return t;
}

template<typename T>
class Monitor
{
    //TODO: reader/writer lock
    std::mutex m;
    T t;
public:

    template<typename F>
    auto get(F f) ->decltype(f(t)) const
    {
        return std::lock_guard<std::mutex>(m),
               f(t);
    }
    
    template<typename F>
    auto mod(F f)
    {
        std::lock_guard<std::mutex> g(m);
        f(t);
    }
};

#endif
