cmake_minimum_required(VERSION 3.14)
project(cubelut VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
# Library-only builds (Espresso XCFramework) skip examples / unit tests / google-benchmark
# to avoid multi-minute FetchContent + configure of unused targets.
option(CUBELUT_BUILD_TOOLS "Build examples, unit tests, and benchmarks" ON)

if(ENABLE_ASAN)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
endif()

# Dependencies: prefer git submodules under third_party/ (offline / no network).
# Fallback: FetchContent only when the submodule tree is missing (first-time clone
# without `git submodule update --init`).
#
#   git submodule update --init --recursive
#
# Pins: highway 1.2.0, fast_float v6.1.1 (see third_party/* checkouts).
include(FetchContent)

set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)

set(_cubelut_hwy_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/highway")
if(EXISTS "${_cubelut_hwy_dir}/CMakeLists.txt")
    message(STATUS "cubelut: using bundled Highway (${_cubelut_hwy_dir})")
    add_subdirectory("${_cubelut_hwy_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/highway-build" EXCLUDE_FROM_ALL)
else()
    message(STATUS "cubelut: Highway submodule missing — FetchContent (needs network)")
    FetchContent_Declare(
        highway
        GIT_REPOSITORY https://github.com/google/highway.git
        GIT_TAG 1.2.0
    )
    FetchContent_MakeAvailable(highway)
endif()

set(_cubelut_ff_dir "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fast_float")
if(EXISTS "${_cubelut_ff_dir}/CMakeLists.txt")
    message(STATUS "cubelut: using bundled fast_float (${_cubelut_ff_dir})")
    add_subdirectory("${_cubelut_ff_dir}" "${CMAKE_CURRENT_BINARY_DIR}/_deps/fast_float-build" EXCLUDE_FROM_ALL)
else()
    message(STATUS "cubelut: fast_float submodule missing — FetchContent (needs network)")
    FetchContent_Declare(
        fast_float
        GIT_REPOSITORY https://github.com/fastfloat/fast_float.git
        GIT_TAG v6.1.1
    )
    FetchContent_MakeAvailable(fast_float)
endif()

# Library
add_library(cubelut
    src/lut.cpp
    src/parser.cpp
    src/processor.cpp
    src/pipeline.cpp
    src/cubelut_c.cpp
    src/baker.cpp
    src/writer.cpp
    src/prepared_lut.cpp
)

target_include_directories(cubelut
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cubelut PUBLIC hwy PRIVATE fast_float)

if(CUBELUT_BUILD_TOOLS)
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

    # Tests
    enable_testing()
    add_executable(cubelut_test tests/test_processor.cpp)
    target_link_libraries(cubelut_test PRIVATE cubelut)
    add_test(NAME processor_test COMMAND cubelut_test)

    add_executable(cubelut_parser_test tests/test_parser.cpp)
    target_link_libraries(cubelut_parser_test PRIVATE cubelut)
    add_test(NAME parser_test COMMAND cubelut_parser_test)

    add_executable(cubelut_c_api_test tests/test_c_api.c)
    target_link_libraries(cubelut_c_api_test PRIVATE cubelut)
    add_test(NAME c_api_test COMMAND cubelut_c_api_test)

    add_executable(cubelut_baker_writer_test tests/test_baker_writer.cpp)
    target_link_libraries(cubelut_baker_writer_test PRIVATE cubelut)
    add_test(NAME baker_writer_test COMMAND cubelut_baker_writer_test)
endif()
