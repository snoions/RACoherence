# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(RACoherence LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Default build type: Release" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE default:" ${CMAKE_BUILD_TYPE})
endif()

set(RUNTIME_LIB
      src/cacheAgent.cpp
      src/clh_mutex.cpp
      src/clh_rwlock.cpp
      src/cxlMalloc.cpp
      src/dlmalloc.c
      src/logger.cpp
      src/runtime.cpp
    )
set_source_files_properties(src/dlmalloc.c PROPERTIES COMPILE_DEFINITIONS "ONLY_MSPACES;HAVE_MMAP=0;USE_LOCKS")

separate_arguments(RAC_FLAG_LIST NATIVE_COMMAND ${RAC_CONFIG_FLAGS})
add_library(racoherence SHARED ${RUNTIME_LIB})
set_property(TARGET racoherence PROPERTY POSITION_INDEPENDENT_CODE ON)
target_compile_options(racoherence PUBLIC
    ${RAC_FLAG_LIST}
    -ftls-model=initial-exec
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fno-extern-tls-init>
)
add_library(racoherence_static STATIC ${RUNTIME_LIB})
target_compile_options(racoherence_static PUBLIC
    ${RAC_FLAG_LIST}
    -ftls-model=local-exec
    $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fno-extern-tls-init>
)
add_executable(main src/main.cpp src/microbench.cpp)
target_link_libraries(main PRIVATE racoherence_static)

include_directories(include)
# Compile options to improve TLS efficiency

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if (ENABLE_ASAN)
    add_compile_options(-fsanitize=address)
    add_link_options(-fsanitize=address)
endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -g")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -g")

set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0 -g")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -g -fno-omit-frame-pointer")

# Jemalloc library
include(ExternalProject)
ExternalProject_Add(jemalloc_proj
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/deps/jemalloc
    INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/install
    CONFIGURE_COMMAND ./autogen.sh --disable-cxx --prefix=<INSTALL_DIR> --with-jemalloc-prefix=je_
    BUILD_COMMAND make
    INSTALL_COMMAND make install
    BUILD_IN_SOURCE 1
)
ExternalProject_Get_Property(jemalloc_proj INSTALL_DIR)
add_dependencies(racoherence jemalloc_proj)
target_link_libraries(racoherence PRIVATE ${INSTALL_DIR}/lib/libjemalloc.so)
target_include_directories(racoherence PRIVATE ${INSTALL_DIR}/include)
add_dependencies(racoherence_static jemalloc_proj)
target_link_libraries(racoherence_static PRIVATE ${INSTALL_DIR}/lib/libjemalloc.so)
target_include_directories(racoherence_static PRIVATE ${INSTALL_DIR}/include)

# Threads library
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
target_link_libraries(racoherence PRIVATE Threads::Threads)
target_link_libraries(racoherence_static PRIVATE Threads::Threads)

set(WBINVD_NAME "wbinvd")
# WBINVD kernel module
add_custom_target(
    insert_wbinvd_module
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deps/wbinvd"
    COMMAND make
    COMMAND sudo rmmod ${WBINVD_NAME}.ko 2>/dev/null | true
    COMMAND sudo insmod ${WBINVD_NAME}.ko
    COMMAND sudo chmod +r /proc/${WBINVD_NAME}
    COMMENT "Building wbinvd module, available at /proc/${WBINVD_NAME}"
)


# Other libraries
target_link_libraries(racoherence PRIVATE ${CMAKE_DL_LIBS})
target_link_libraries(racoherence_static PRIVATE ${CMAKE_DL_LIBS})

# NUMA library
find_library(NUMA_LIB NAMES numa)
if(NUMA_LIB)
    message(STATUS "Found libnuma at: ${NUMA_LIB}")
    target_link_libraries(racoherence PUBLIC ${NUMA_LIB})
    target_link_libraries(racoherence_static PUBLIC ${NUMA_LIB})
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
    target_link_libraries(tests test_lib gtest_main ${INSTALL_DIR}/lib/libjemalloc.a)

    include(GoogleTest)
    gtest_discover_tests(tests)
endif()
