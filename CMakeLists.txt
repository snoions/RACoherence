# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(RACoherence LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Default build type: RelWithDebInfo" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE default:" ${CMAKE_BUILD_TYPE})
endif()

set(INDEX_FILES
      src/main.cpp
      src/logBuffer.cpp
      src/user.cpp
      src/cacheAgent.cpp
    )

add_executable(main ${INDEX_FILES})

target_include_directories(main PUBLIC include)

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -fno-omit-frame-pointer -fPIC")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -fno-omit-frame-pointer -fPIC")

# Threads library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(main PRIVATE Threads::Threads)

# NUMA library
find_library(NUMA_LIB NAMES numa)
if(NUMA_LIB)
    message(STATUS "Found libnuma at: ${NUMA_LIB}")
    target_link_libraries(main PRIVATE ${NUMA_LIB})
else()
    message(FATAL_ERROR "libnuma not found!")
endif()

# Fetch GoogleTest
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/release-1.12.1.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Add the header-only library
add_library(cache_tracker INTERFACE)
target_include_directories(cache_tracker INTERFACE include)

# Add test
enable_testing()
add_executable(tests test/testCacheTracker.cpp)
target_link_libraries(tests cache_tracker gtest_main)

include(GoogleTest)
gtest_discover_tests(tests)
