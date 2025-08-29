# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(RACoherence LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type: Release" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE default:" ${CMAKE_BUILD_TYPE})
endif()

set(INDEX_FILES
      src/cacheAgent.cpp
      src/clh_mutex.cpp
      src/cxlMalloc.cpp
      src/dlmalloc.c
      src/logger.cpp
      src/main.cpp
      src/threadOps.cpp
      src/user.cpp
    )
set_source_files_properties(src/dlmalloc.c PROPERTIES COMPILE_DEFINITIONS "ONLY_MSPACES;HAVE_MMAP=0;USE_LOCKS")

add_executable(main ${INDEX_FILES})
target_include_directories(main PUBLIC include)
# Compile options to improve TLS efficiency
target_compile_options(main PUBLIC
    -ftls-model=initial-exec
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fno-extern-tls-init>
)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if (ENABLE_ASAN)
    target_compile_options(main PUBLIC -fsanitize=address)
    target_link_options(main PUBLIC -fsanitize=address)
endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -fno-omit-frame-pointer")

# find_package(PkgConfig REQUIRED)
#
# # Jemalloc library
# pkg_check_modules(JEMALLOC REQUIRED jemalloc)
# include_directories(${JEMALLOC_INCLUDE_DIRS})
# target_link_libraries(main PRIVATE ${JEMALLOC_LIBRARIES})

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

option(ENABLE_TESTS "Enable Tests" OFF)
if(ENABLE_TESTS)
    # Fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/release-1.12.1.zip
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_library(test_lib INTERFACE)
    target_include_directories(test_lib INTERFACE include)

    # Add test
    enable_testing()
    add_executable(tests test/testCacheTracker.cpp test/testMemoryPool.cpp)
    target_link_libraries(tests test_lib gtest_main)

    include(GoogleTest)
    gtest_discover_tests(tests)
endif()
