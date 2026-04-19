#include <benchmark/benchmark.h>
#include <vector>
#include <random>
#include <iostream>
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
static std::vector<float> cached_img_1080p;
static std::vector<float> cached_img_4k;
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
        cached_img_4k = generate_random_image(3840, 2160);
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

BENCHMARK_MAIN();
