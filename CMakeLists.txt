# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(RACoherence LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(INDEX_FILES
      src/main.cpp
      src/logBuffer.cpp
      src/user.cpp
      src/cacheAgent.cpp
    )

add_executable(main ${INDEX_FILES})

target_include_directories(main PUBLIC include)
target_compile_options(main PRIVATE -g)

# Threads library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(main PRIVATE Threads::Threads)

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
