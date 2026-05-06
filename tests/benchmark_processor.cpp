#include <benchmark/benchmark.h>
#include <vector>
#include <random>
#include <iostream>
#include <thread>
#include "cubelut/parser.hpp"
#include "cubelut/processor.hpp"
#include "cubelut/cubelut_c.h"

// Generate a random RGB float image
static std::vector<float> generate_random_image(size_t width, size_t height) {
    std::vector<float> img(width * height * 3);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(0.0f, 1.0f);

    for (size_t i = 0; i < img.size(); ++i) {
        img[i] = dis(gen);
    }
    return img;
}

// Global cached resources to prevent reloading files for each iteration
static std::optional<cubelut::Lut> cached_lut_33;
static std::vector<float>    cached_img_1080p;
static std::vector<float>    cached_img_4k;
static std::vector<uint8_t>  cached_img_u8_4k;    // uint8 RGB 4K
static std::vector<uint8_t>  cached_img_rgba8_4k;  // uint8 RGBA 4K
static cubelut_lut_t* cached_c_lut = nullptr;

static void SetupBenchmark() {
    if (!cached_lut_33) {
        cached_lut_33 = cubelut::Parser::fromFile("../tests/files/FLog2C_to_ACROS_33grid_V.1.00.cube");
        if (!cached_lut_33 || !cached_lut_33->isValid()) {
            std::cerr << "Failed to load LUT for benchmarks!" << std::endl;
            exit(1);
        }
        cached_c_lut = cubelut_load_from_file("../tests/files/FLog2C_to_ACROS_33grid_V.1.00.cube");
        
        cached_img_1080p = generate_random_image(1920, 1080);
        cached_img_4k    = generate_random_image(3840, 2160);

        // Build uint8 equivalents (float → uint8 via truncation)
        const size_t px4k = 3840 * 2160;
        cached_img_u8_4k.resize(px4k * 3);
        cached_img_rgba8_4k.resize(px4k * 4);
        for (size_t i = 0; i < px4k; ++i) {
            cached_img_u8_4k[i*3+0] = static_cast<uint8_t>(cached_img_4k[i*3+0]*255);
            cached_img_u8_4k[i*3+1] = static_cast<uint8_t>(cached_img_4k[i*3+1]*255);
            cached_img_u8_4k[i*3+2] = static_cast<uint8_t>(cached_img_4k[i*3+2]*255);
            cached_img_rgba8_4k[i*4+0] = cached_img_u8_4k[i*3+0];
            cached_img_rgba8_4k[i*4+1] = cached_img_u8_4k[i*3+1];
            cached_img_rgba8_4k[i*4+2] = cached_img_u8_4k[i*3+2];
            cached_img_rgba8_4k[i*4+3] = 255;
        }
    }
}

// ---------------------------------------------------------
// Parsing Benchmarks
// ---------------------------------------------------------

static void BM_Parse_LUT_33(benchmark::State& state) {
    for (auto _ : state) {
        auto lut = cubelut::Parser::fromFile("../tests/files/FLog2C_to_ACROS_33grid_V.1.00.cube");
        benchmark::DoNotOptimize(lut);
    }
}
BENCHMARK(BM_Parse_LUT_33)->Unit(benchmark::kMillisecond);

static void BM_Parse_LUT_65(benchmark::State& state) {
    for (auto _ : state) {
        auto lut = cubelut::Parser::fromFile("../tests/files/FLog2C_to_ACROS_65grid_V.1.00.cube");
        benchmark::DoNotOptimize(lut);
    }
}
BENCHMARK(BM_Parse_LUT_65)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------
// CPU Processing Benchmarks
// ---------------------------------------------------------

static void BM_ProcessImage_Tetrahedral_1080p(benchmark::State& state) {
    SetupBenchmark();
    std::vector<float> img = cached_img_1080p; // Copy to process in-place

    for (auto _ : state) {
        cubelut::Processor::processImage(*cached_lut_33, img.data(), 1920, 1080, cubelut::Interpolation::Tetrahedral);
        benchmark::DoNotOptimize(img.data());
    }

    state.counters["Pixels/sec"] = benchmark::Counter(state.iterations() * 1920 * 1080, benchmark::Counter::kIsRate);
    state.counters["Megapixels/sec"] = benchmark::Counter(state.iterations() * 1920 * 1080, benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_Tetrahedral_1080p)->Unit(benchmark::kMillisecond);

static void BM_ProcessImage_Trilinear_1080p(benchmark::State& state) {
    SetupBenchmark();
    std::vector<float> img = cached_img_1080p;

    for (auto _ : state) {
        cubelut::Processor::processImage(*cached_lut_33, img.data(), 1920, 1080, cubelut::Interpolation::Trilinear);
        benchmark::DoNotOptimize(img.data());
    }

    state.counters["Megapixels/sec"] = benchmark::Counter(state.iterations() * 1920 * 1080, benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_Trilinear_1080p)->Unit(benchmark::kMillisecond);

static void BM_ProcessImage_Tetrahedral_4K(benchmark::State& state) {
    SetupBenchmark();
    std::vector<float> img = cached_img_4k; 

    for (auto _ : state) {
        cubelut::Processor::processImage(*cached_lut_33, img.data(), 3840, 2160, cubelut::Interpolation::Tetrahedral);
        benchmark::DoNotOptimize(img.data());
    }

    state.counters["Megapixels/sec"] = benchmark::Counter(state.iterations() * 3840 * 2160, benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_Tetrahedral_4K)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------
// C-API Export Benchmarks (Data Packing)
// ---------------------------------------------------------

static void BM_C_API_Export_RGBA32(benchmark::State& state) {
    SetupBenchmark();
    size_t byte_size = 0;
    
    for (auto _ : state) {
        float* buf = cubelut_create_rgba_buffer_for_grid3d(cached_c_lut, &byte_size);
        benchmark::DoNotOptimize(buf);
        cubelut_free_buffer(buf);
    }
}
BENCHMARK(BM_C_API_Export_RGBA32)->Unit(benchmark::kMicrosecond);

static void BM_C_API_Export_RGBA16(benchmark::State& state) {
    SetupBenchmark();
    size_t byte_size = 0;
    
    for (auto _ : state) {
        uint16_t* buf = cubelut_create_rgba16_buffer_for_grid3d(cached_c_lut, &byte_size);
        benchmark::DoNotOptimize(buf);
        cubelut_free_buffer(buf);
    }
}
BENCHMARK(BM_C_API_Export_RGBA16)->Unit(benchmark::kMicrosecond);

// ---------------------------------------------------------
// Parallel Processing Benchmarks
// ---------------------------------------------------------

// Scaling curve: 1/2/4/8/10 threads on 4K image
static void BM_ProcessImage_Parallel_4K(benchmark::State& state) {
    SetupBenchmark();
    const unsigned NT = static_cast<unsigned>(state.range(0));
    std::vector<float> img = cached_img_4k;
    for (auto _ : state) {
        cubelut::Processor::processImageParallel(
            *cached_lut_33, img.data(), 3840, 2160,
            cubelut::Interpolation::Tetrahedral, NT);
        benchmark::DoNotOptimize(img.data());
    }
    const double mpx = 3840.0 * 2160.0 / 1e6;
    state.counters["Threads"]        = NT;
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * mpx * 1e6,
                           benchmark::Counter::kIsRate) / 1e6;
}
// Thread scaling: 1 thread (== processImage baseline), 2, 4, 8, 10 (all cores)
BENCHMARK(BM_ProcessImage_Parallel_4K)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(10)
    ->Unit(benchmark::kMillisecond);

// Auto-thread (hardware_concurrency) convenience benchmark
static void BM_ProcessImage_Parallel_4K_Auto(benchmark::State& state) {
    SetupBenchmark();
    std::vector<float> img = cached_img_4k;
    for (auto _ : state) {
        cubelut::Processor::processImageParallel(
            *cached_lut_33, img.data(), 3840, 2160);
        benchmark::DoNotOptimize(img.data());
    }
    state.counters["Threads"]        = std::thread::hardware_concurrency();
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_Parallel_4K_Auto)->Unit(benchmark::kMillisecond);

// 1080p auto-parallel (baseline was 11ms, target < 3ms)
static void BM_ProcessImage_Parallel_1080p_Auto(benchmark::State& state) {
    SetupBenchmark();
    std::vector<float> img = cached_img_1080p;
    for (auto _ : state) {
        cubelut::Processor::processImageParallel(
            *cached_lut_33, img.data(), 1920, 1080);
        benchmark::DoNotOptimize(img.data());
    }
    state.counters["Threads"]        = std::thread::hardware_concurrency();
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 1920.0 * 1080.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_Parallel_1080p_Auto)->Unit(benchmark::kMillisecond);

// getParallelChunks overhead (should be < 5 µs)
static void BM_GetParallelChunks(benchmark::State& state) {
    for (auto _ : state) {
        auto chunks = cubelut::Processor::getParallelChunks(3840, 2160, 0);
        benchmark::DoNotOptimize(chunks.data());
    }
}
BENCHMARK(BM_GetParallelChunks)->Unit(benchmark::kMicrosecond);

// C API: cubelut_process_image_parallel
static void BM_C_API_Parallel_4K(benchmark::State& state) {
    SetupBenchmark();
    const unsigned NT = static_cast<unsigned>(state.range(0));
    std::vector<float> img = cached_img_4k;
    for (auto _ : state) {
        cubelut_process_image_parallel(cached_c_lut, img.data(), 3840, 2160, true, NT);
        benchmark::DoNotOptimize(img.data());
    }
    state.counters["Threads"]        = NT;
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_C_API_Parallel_4K)->Arg(0)->Arg(8)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------
// uint8 Processing Benchmarks (vs float32 baseline)
// ---------------------------------------------------------

// Single-thread uint8 RGB 4K (compare against BM_ProcessImage_Tetrahedral_4K ~40ms)
static void BM_ProcessImage_U8_4K(benchmark::State& state) {
    SetupBenchmark();
    std::vector<uint8_t> img = cached_img_u8_4k;
    std::vector<uint8_t> out(img.size());
    for (auto _ : state) {
        cubelut::Processor::processImageU8(
            *cached_lut_33, img.data(), out.data(), 3840, 2160);
        benchmark::DoNotOptimize(out.data());
    }
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_U8_4K)->Unit(benchmark::kMillisecond);

// Parallel uint8 RGB 4K (auto-thread)
static void BM_ProcessImage_U8_Parallel_4K(benchmark::State& state) {
    SetupBenchmark();
    std::vector<uint8_t> img = cached_img_u8_4k;
    std::vector<uint8_t> out(img.size());
    for (auto _ : state) {
        cubelut::Processor::processImageU8Parallel(
            *cached_lut_33, img.data(), out.data(), 3840, 2160);
        benchmark::DoNotOptimize(out.data());
    }
    state.counters["Threads"] = std::thread::hardware_concurrency();
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_U8_Parallel_4K)->Unit(benchmark::kMillisecond);

// RGBA8 4K (compare against RGB8)
static void BM_ProcessImage_RGBA8_4K(benchmark::State& state) {
    SetupBenchmark();
    std::vector<uint8_t> img = cached_img_rgba8_4k;
    std::vector<uint8_t> out(img.size());
    for (auto _ : state) {
        cubelut::Processor::processImageRGBA8(
            *cached_lut_33, img.data(), out.data(), 3840, 2160);
        benchmark::DoNotOptimize(out.data());
    }
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_RGBA8_4K)->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------
// float32 PixelLayout benchmarks (single-pass SIMD, zero temp buffer)
// Compare against BM_ProcessImage_Tetrahedral_4K (~40ms RGB baseline)
// ---------------------------------------------------------

static std::vector<float> make_rgba_f32_4k() {
    SetupBenchmark();
    const size_t px = 3840 * 2160;
    std::vector<float> img(px * 4);
    for (size_t i = 0; i < px; ++i) {
        img[i*4+0] = cached_img_4k[i*3+0];
        img[i*4+1] = cached_img_4k[i*3+1];
        img[i*4+2] = cached_img_4k[i*3+2];
        img[i*4+3] = 1.0f;
    }
    return img;
}

// float32 RGBA single-thread (single-pass SIMD, no temp buffer)
static void BM_ProcessImage_RGBA_F32_4K(benchmark::State& state) {
    auto img = make_rgba_f32_4k();
    for (auto _ : state) {
        cubelut::Processor::processImage(
            *cached_lut_33, img.data(), 3840, 2160,
            cubelut::PixelLayout::RGBA_F32);
        benchmark::DoNotOptimize(img.data());
    }
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_RGBA_F32_4K)->Unit(benchmark::kMillisecond);

// float32 BGRA single-thread (Metal MTLPixelFormatBGRA32Float layout)
static void BM_ProcessImage_BGRA_F32_4K(benchmark::State& state) {
    auto img = make_rgba_f32_4k();
    // Reinterpret as BGRA by swapping R↔B (for benchmark purposes)
    const size_t px = 3840 * 2160;
    for (size_t i = 0; i < px; ++i) std::swap(img[i*4+0], img[i*4+2]);
    for (auto _ : state) {
        cubelut::Processor::processImage(
            *cached_lut_33, img.data(), 3840, 2160,
            cubelut::PixelLayout::BGRA_F32);
        benchmark::DoNotOptimize(img.data());
    }
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_BGRA_F32_4K)->Unit(benchmark::kMillisecond);

// float32 RGBA parallel (GCD / std::async)
static void BM_ProcessImage_RGBA_F32_Parallel_4K(benchmark::State& state) {
    auto img = make_rgba_f32_4k();
    for (auto _ : state) {
        cubelut::Processor::processImageParallel(
            *cached_lut_33, img.data(), 3840, 2160,
            cubelut::PixelLayout::RGBA_F32);
        benchmark::DoNotOptimize(img.data());
    }
    state.counters["Threads"] = std::thread::hardware_concurrency();
    state.counters["Megapixels/sec"] =
        benchmark::Counter(state.iterations() * 3840.0 * 2160.0,
                           benchmark::Counter::kIsRate) / 1e6;
}
BENCHMARK(BM_ProcessImage_RGBA_F32_Parallel_4K)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();
