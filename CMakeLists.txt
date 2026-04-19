cmake_minimum_required(VERSION 3.14)
project(cubelut VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Fetch Google Highway for SIMD optimizations
include(FetchContent)
FetchContent_Declare(
    highway
    GIT_REPOSITORY https://github.com/google/highway.git
    GIT_TAG 1.2.0
)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(highway)

# Fetch fast_float for blazing fast parsing
FetchContent_Declare(
    fast_float
    GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
    GIT_TAG v6.1.1
)
FetchContent_MakeAvailable(fast_float)

# Library
add_library(cubelut
    src/lut.cpp
    src/parser.cpp
    src/processor.cpp
    src/cubelut_c.cpp
)

target_include_directories(cubelut
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cubelut PUBLIC hwy PRIVATE fast_float)

# Examples
add_executable(cubelut_example examples/basic_usage.cpp)
target_link_libraries(cubelut_example PRIVATE cubelut)

add_executable(apply_lut examples/apply_lut.cpp)
target_include_directories(apply_lut PRIVATE vendor/stb)
target_link_libraries(apply_lut PRIVATE cubelut)

# Fetch Google Benchmark for performance testing
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# Benchmark Target
add_executable(cubelut_benchmark tests/benchmark_processor.cpp)
target_link_libraries(cubelut_benchmark PRIVATE cubelut benchmark::benchmark)

add_executable(cubelut_test tests/test_processor.cpp)
target_link_libraries(cubelut_test PRIVATE cubelut)
add_test(NAME processor_test COMMAND cubelut_test)

add_executable(cubelut_parser_test tests/test_parser.cpp)
target_link_libraries(cubelut_parser_test PRIVATE cubelut)
add_test(NAME parser_test COMMAND cubelut_parser_test)

add_executable(cubelut_c_api_test tests/test_c_api.c)
target_link_libraries(cubelut_c_api_test PRIVATE cubelut)
add_test(NAME c_api_test COMMAND cubelut_c_api_test)
