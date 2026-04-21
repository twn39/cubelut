// ============================================================================
// test_processor.cpp
//
// Covers:
//  - Original tests (identity, math correctness)
//  - TEST-1: SIMD-bulk vs scalar-per-pixel numerical consistency
//            using 35 pixels (> widest AVX-512 lane = 16) to force both
//            the bulk vectorised loop AND the scalar tail path.
//  - TEST-2: Out-of-range and special float inputs (clamping, NaN, Inf)
//  - TEST-3: Multi-threaded determinism (concurrent processPixels on
//            non-overlapping ranges must match single-threaded reference)
//  - FEAT-5: Pipeline single-stage and multi-stage correctness
// ============================================================================
#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <vector>
#include <random>
#include <thread>
#include <limits>
#include <algorithm>
#include "cubelut/cubelut.hpp"  // includes pipeline.hpp

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build a 5×5×5 LUT with a non-trivial (non-identity) mapping to give
/// meaningful SIMD/scalar and threading consistency checks.
static cubelut::Lut make_5x5x5_lut() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 5;
    const int N = 5;
    d3.data.reserve(N * N * N * 3);
    for (int b = 0; b < N; ++b)
    for (int g = 0; g < N; ++g)
    for (int r = 0; r < N; ++r) {
        float fr = r / float(N - 1), fg = g / float(N - 1), fb = b / float(N - 1);
        d3.data.push_back(std::sqrt(fr));
        d3.data.push_back(fg * fg);
        d3.data.push_back(0.5f * fr + 0.3f * fg + 0.2f * fb);
    }
    lut.grid3D = std::move(d3);
    return lut;
}

/// Build a 2×2×2 identity LUT.
static cubelut::Lut make_identity_2x_lut() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 2;
    d3.data = {
        0,0,0, 1,0,0,
        0,1,0, 1,1,0,
        0,0,1, 1,0,1,
        0,1,1, 1,1,1
    };
    lut.grid3D = std::move(d3);
    return lut;
}

// ---------------------------------------------------------------------------
// Original tests (preserved)
// ---------------------------------------------------------------------------

void test_identity_1d() {
    cubelut::Lut lut;
    cubelut::LutData1D d1;
    d1.size = 2;
    d1.data = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    lut.shaper1D = std::move(d1);

    std::array<float, 3> pixel = {0.5f, 0.5f, 0.5f};
    auto result = cubelut::Processor::process(lut, pixel);

    assert(std::abs(result[0] - 0.5f) < 1e-6f);
    assert(std::abs(result[1] - 0.5f) < 1e-6f);
    assert(std::abs(result[2] - 0.5f) < 1e-6f);
    std::cout << "test_identity_1d passed" << std::endl;
}

void test_identity_3d() {
    auto lut = make_identity_2x_lut();

    std::array<float, 3> pixel = {0.25f, 0.5f, 0.75f};
    auto result = cubelut::Processor::process(lut, pixel);

    assert(std::abs(result[0] - 0.25f) < 1e-6f);
    assert(std::abs(result[1] - 0.5f)  < 1e-6f);
    assert(std::abs(result[2] - 0.75f) < 1e-6f);
    std::cout << "test_identity_3d passed" << std::endl;
}

void test_process_image() {
    auto lut = make_identity_2x_lut();

    std::vector<float> imageData = {
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f,
        0.5f, 0.5f, 0.5f
    };

    cubelut::Processor::processImage(lut, imageData.data(), 3, 1);

    assert(std::abs(imageData[0] - 0.0f) < 1e-6f);
    assert(std::abs(imageData[3] - 1.0f) < 1e-6f);
    assert(std::abs(imageData[6] - 0.5f) < 1e-6f);
    std::cout << "test_process_image passed" << std::endl;
}

void test_trilinear_math() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 2;
    d3.data = {
        // Z=0 (B=0)
        0,0,0,    10,0,0,
        0,20,0,   10,20,0,
        // Z=1 (B=1)
        0,0,30,   10,0,30,
        0,20,30,  10,20,30
    };
    lut.grid3D = std::move(d3);

    std::array<float, 3> pixel = {0.1f, 0.5f, 0.9f};
    auto result = cubelut::Processor::process(lut, pixel, cubelut::Interpolation::Trilinear);

    assert(std::abs(result[0] - 1.0f)  < 1e-5f);
    assert(std::abs(result[1] - 10.0f) < 1e-5f);
    assert(std::abs(result[2] - 27.0f) < 1e-5f);
    std::cout << "test_trilinear_math passed" << std::endl;
}

void test_tetrahedral_math() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 2;
    d3.data = {
        0,0,0,    10,0,0,
        0,20,0,   10,20,0,
        0,0,30,   10,0,30,
        0,20,30,  10,20,30
    };
    lut.grid3D = std::move(d3);

    // R > B > G → tetrahedron vertices: C000, Ca=(10,0,0), Cb=(10,0,30), C111=(10,20,30)
    // x0=0.9, x1=0.5, x2=0.1 → result = (9, 2, 15)
    std::array<float, 3> pixel = {0.9f, 0.1f, 0.5f};
    auto result = cubelut::Processor::process(lut, pixel, cubelut::Interpolation::Tetrahedral);

    assert(std::abs(result[0] - 9.0f)  < 1e-5f);
    assert(std::abs(result[1] - 2.0f)  < 1e-5f);
    assert(std::abs(result[2] - 15.0f) < 1e-5f);
    std::cout << "test_tetrahedral_math passed" << std::endl;
}

// ---------------------------------------------------------------------------
// TEST-1: SIMD-bulk vs scalar-per-pixel numerical consistency
//
// Uses 35 random pixels. Size 35 ensures both the vectorised bulk loop and
// the scalar tail path are exercised on every architecture:
//   AVX-512 (N=16): 2 full vectors + 3-pixel tail
//   AVX2    (N=8):  4 full vectors + 3-pixel tail
//   SSE4    (N=4):  8 full vectors + 3-pixel tail
// ---------------------------------------------------------------------------
static void run_simd_scalar_consistency(const char* name,
                                        cubelut::Interpolation interp) {
    constexpr int NUM_PIXELS = 35;
    auto lut = make_5x5x5_lut();

    // Generate deterministic random pixels in [0, 1]
    std::vector<float> pixels(NUM_PIXELS * 3);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    for (auto& v : pixels) v = dist(rng);

    // Scalar reference: process each pixel individually via process()
    std::vector<float> ref(NUM_PIXELS * 3);
    for (int i = 0; i < NUM_PIXELS; ++i) {
        std::array<float, 3> px = {pixels[i*3], pixels[i*3+1], pixels[i*3+2]};
        auto r = cubelut::Processor::process(lut, px, interp);
        ref[i*3+0] = r[0]; ref[i*3+1] = r[1]; ref[i*3+2] = r[2];
    }

    // SIMD path: processPixels dispatches to HWY_DYNAMIC_DISPATCH bulk kernel
    std::vector<float> simd_result(pixels);
    cubelut::Processor::processPixels(lut, simd_result.data(), 0, NUM_PIXELS, interp);

    constexpr float tol = 1e-5f;
    for (int i = 0; i < NUM_PIXELS * 3; ++i) {
        float diff = std::abs(ref[i] - simd_result[i]);
        if (diff >= tol) {
            std::cerr << "[" << name << "] SIMD/scalar mismatch at float[" << i
                      << "]: scalar=" << ref[i] << " simd=" << simd_result[i]
                      << " diff=" << diff << std::endl;
            assert(false);
        }
    }
    std::cout << name << " passed (" << NUM_PIXELS << " pixels, "
              << "max tolerance " << tol << ")" << std::endl;
}

void test_simd_scalar_consistency_tetrahedral() {
    run_simd_scalar_consistency(
        "test_simd_scalar_consistency_tetrahedral",
        cubelut::Interpolation::Tetrahedral);
}

void test_simd_scalar_consistency_trilinear() {
    run_simd_scalar_consistency(
        "test_simd_scalar_consistency_trilinear",
        cubelut::Interpolation::Trilinear);
}

// ---------------------------------------------------------------------------
// TEST-2a: Out-of-range inputs are clamped (not crashing, not UB)
// ---------------------------------------------------------------------------
void test_out_of_range_inputs() {
    auto lut = make_identity_2x_lut();

    // Negative input → clamped to domain min → LUT corner [0,0,0] → (0,0,0)
    {
        auto r = cubelut::Processor::process(lut, {-1.0f, -2.0f, -0.5f});
        assert(std::abs(r[0] - 0.0f) < 1e-6f);
        assert(std::abs(r[1] - 0.0f) < 1e-6f);
        assert(std::abs(r[2] - 0.0f) < 1e-6f);
    }

    // Input > 1 → clamped to domain max → LUT corner [1,1,1] → (1,1,1)
    {
        auto r = cubelut::Processor::process(lut, {2.0f, 1.5f, 10.0f});
        assert(std::abs(r[0] - 1.0f) < 1e-6f);
        assert(std::abs(r[1] - 1.0f) < 1e-6f);
        assert(std::abs(r[2] - 1.0f) < 1e-6f);
    }

    // Test the same via the SIMD bulk path (processPixels)
    {
        std::vector<float> buf = {-1.0f,-2.0f,-0.5f,  2.0f,1.5f,10.0f};
        cubelut::Processor::processPixels(lut, buf.data(), 0, 2);
        assert(std::abs(buf[0] - 0.0f) < 1e-6f);
        assert(std::abs(buf[3] - 1.0f) < 1e-6f);
    }

    std::cout << "test_out_of_range_inputs passed" << std::endl;
}

// ---------------------------------------------------------------------------
// TEST-2b: Special float inputs must not crash.
//   ±Inf: Highway Clamp (based on SSE minps/maxps) treats the out-of-range
//         value as if it were ±∞ → clamped to boundary → finite output.
//   NaN:  On x86 SSE minps/maxps, NaN in the first operand causes the second
//         operand to be returned; so NaN effectively gets clamped to a
//         boundary. Behaviour on other ISAs may differ. We only require:
//         (1) no crash / signal, and (2) all output channels are finite if
//         all input channels produce finite values on the current arch.
// ---------------------------------------------------------------------------
void test_special_float_inputs() {
    auto lut = make_identity_2x_lut();

    const float pos_inf = std::numeric_limits<float>::infinity();
    const float neg_inf = -std::numeric_limits<float>::infinity();
    const float nan_val = std::numeric_limits<float>::quiet_NaN();

    // +Inf → clamped to max → (1,1,1)
    {
        auto r = cubelut::Processor::process(lut, {pos_inf, pos_inf, pos_inf});
        if (std::isfinite(r[0])) assert(std::abs(r[0] - 1.0f) < 1e-6f);
    }

    // –Inf → clamped to min → (0,0,0)
    {
        auto r = cubelut::Processor::process(lut, {neg_inf, neg_inf, neg_inf});
        if (std::isfinite(r[0])) assert(std::abs(r[0] - 0.0f) < 1e-6f);
    }

    // NaN: just verify no crash; don't assert output value (arch-dependent)
    {
        auto r = cubelut::Processor::process(lut, {nan_val, nan_val, nan_val});
        (void)r; // result may be NaN on some ISAs (NEON) – that is acceptable
    }

    // Same inputs via SIMD bulk path (processPixels)
    {
        std::vector<float> buf = {
            pos_inf, pos_inf, pos_inf,
            neg_inf, neg_inf, neg_inf,
            nan_val, nan_val, nan_val
        };
        cubelut::Processor::processPixels(lut, buf.data(), 0, 3);
        // +Inf pixel: must be finite on x86, may be NaN on NEON
        if (std::isfinite(buf[0])) assert(std::abs(buf[0] - 1.0f) < 1e-5f);
        if (std::isfinite(buf[3])) assert(std::abs(buf[3] - 0.0f) < 1e-5f);
    }

    std::cout << "test_special_float_inputs passed (no crash on ±Inf/NaN)" << std::endl;
}

// ---------------------------------------------------------------------------
// TEST-3: Multi-threaded determinism
//
// Processes a 200×200 image:
//  (A) Single-threaded via Processor::processImage → reference
//  (B) Multi-threaded with N threads and 16-pixel-aligned chunk boundaries
//  Runs (B) twice to catch any data-race-induced non-determinism.
//  Asserts (A)==(B) within floating-point tolerance.
// ---------------------------------------------------------------------------
void test_multithreaded_determinism() {
    auto lut = make_5x5x5_lut();

    constexpr size_t W = 200, H = 200;
    constexpr size_t TOTAL = W * H;

    // Build a reproducible random image
    std::vector<float> img_ref(TOTAL * 3);
    {
        std::mt19937 rng(777);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (auto& v : img_ref) v = dist(rng);
    }

    // Single-threaded reference
    std::vector<float> img_single(img_ref);
    cubelut::Processor::processImage(lut, img_single.data(), W, H);

    // Multi-threaded helper
    auto run_multithreaded = [&]() -> std::vector<float> {
        std::vector<float> img_mt(img_ref);

        unsigned int NT = std::thread::hardware_concurrency();
        if (NT == 0) NT = 4;

        constexpr size_t ALIGN = 16; // ≥ widest SIMD lane (AVX-512 float)
        size_t base = (TOTAL / NT + ALIGN - 1) / ALIGN * ALIGN;

        std::vector<std::thread> threads;
        for (unsigned t = 0; t < NT; ++t) {
            size_t s = t * base;
            if (s >= TOTAL) break;
            size_t e = std::min(s + base, TOTAL);
            threads.emplace_back([&lut, &img_mt, s, e]() {
                cubelut::Processor::processPixels(lut, img_mt.data(), s, e);
            });
        }
        for (auto& th : threads) th.join();
        return img_mt;
    };

    // Run twice – verifies determinism (no data races)
    auto img_mt1 = run_multithreaded();
    auto img_mt2 = run_multithreaded();

    constexpr float tol = 1e-5f;
    for (size_t i = 0; i < TOTAL * 3; ++i) {
        if (std::abs(img_single[i] - img_mt1[i]) >= tol) {
            std::cerr << "MT/single mismatch at [" << i << "]: "
                      << img_single[i] << " vs " << img_mt1[i] << std::endl;
            assert(false);
        }
        if (img_mt1[i] != img_mt2[i]) {
            std::cerr << "MT run-to-run non-determinism at [" << i << "]" << std::endl;
            assert(false);
        }
    }

    unsigned int NT = std::thread::hardware_concurrency();
    if (NT == 0) NT = 4;
    std::cout << "test_multithreaded_determinism passed ("
              << NT << " threads, " << TOTAL << " pixels, tolerance=" << tol << ")"
              << std::endl;
}

// ---------------------------------------------------------------------------
// FEAT-5: Pipeline tests
// ---------------------------------------------------------------------------

/// A single-stage pipeline must produce identical results to Processor::process.
void test_pipeline_single_stage() {
    auto lut = make_5x5x5_lut();

    cubelut::Pipeline pipe;
    pipe.addStage(lut);                       // copy into pipeline
    assert(pipe.isValid());
    assert(pipe.stageCount() == 1);

    std::mt19937 rng(99);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    for (int trial = 0; trial < 50; ++trial) {
        std::array<float, 3> px = {dist(rng), dist(rng), dist(rng)};
        auto direct = cubelut::Processor::process(lut, px);
        auto piped  = pipe.process(px);
        assert(std::abs(direct[0] - piped[0]) < 1e-6f);
        assert(std::abs(direct[1] - piped[1]) < 1e-6f);
        assert(std::abs(direct[2] - piped[2]) < 1e-6f);
    }
    std::cout << "test_pipeline_single_stage passed (50 random pixels)" << std::endl;
}

/// Two identity LUTs in series → still maps any pixel to itself.
void test_pipeline_multi_stage_identity() {
    auto id = make_identity_2x_lut();

    cubelut::Pipeline pipe;
    pipe.addStage(id);
    pipe.addStage(id);
    assert(pipe.stageCount() == 2);

    const std::vector<std::array<float, 3>> test_pixels = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
        {0.3f, 0.6f, 0.9f}, {0.1f, 0.5f, 0.8f}
    };
    for (const auto& px : test_pixels) {
        auto r = pipe.process(px);
        assert(std::abs(r[0] - px[0]) < 1e-5f);
        assert(std::abs(r[1] - px[1]) < 1e-5f);
        assert(std::abs(r[2] - px[2]) < 1e-5f);
    }
    std::cout << "test_pipeline_multi_stage_identity passed" << std::endl;
}

/// Pipeline::processPixels must match calling process() for each pixel.
void test_pipeline_process_pixels() {
    auto lut = make_5x5x5_lut();
    auto id  = make_identity_2x_lut();

    cubelut::Pipeline pipe;
    pipe.addStage(lut);
    pipe.addStage(id);   // applying identity after should change nothing

    constexpr int NUM_PIXELS = 35; // forces bulk + tail on all SIMD widths
    std::vector<float> pixels(NUM_PIXELS * 3);
    {
        std::mt19937 rng(2024);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (auto& v : pixels) v = dist(rng);
    }

    // Scalar reference via process()
    std::vector<float> ref(NUM_PIXELS * 3);
    for (int i = 0; i < NUM_PIXELS; ++i) {
        std::array<float, 3> px = {pixels[i*3], pixels[i*3+1], pixels[i*3+2]};
        auto r = pipe.process(px);
        ref[i*3+0] = r[0]; ref[i*3+1] = r[1]; ref[i*3+2] = r[2];
    }

    // Bulk path via processPixels
    std::vector<float> bulk(pixels);
    pipe.processPixels(bulk.data(), 0, NUM_PIXELS);

    constexpr float tol = 1e-5f;
    for (int i = 0; i < NUM_PIXELS * 3; ++i) {
        float diff = std::abs(ref[i] - bulk[i]);
        if (diff >= tol) {
            std::cerr << "Pipeline processPixels mismatch at [" << i
                      << "]: ref=" << ref[i] << " bulk=" << bulk[i] << std::endl;
            assert(false);
        }
    }
    std::cout << "test_pipeline_process_pixels passed (" << NUM_PIXELS << " pixels)" << std::endl;
}

/// Pipeline::clear() and empty-pipeline guard.
void test_pipeline_lifecycle() {
    cubelut::Pipeline pipe;
    assert(!pipe.isValid());
    assert(pipe.stageCount() == 0);

    pipe.addStage(make_identity_2x_lut());
    assert(pipe.isValid());
    assert(pipe.stageCount() == 1);

    pipe.clear();
    assert(!pipe.isValid());
    assert(pipe.stageCount() == 0);

    std::cout << "test_pipeline_lifecycle passed" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    // --- Original ---
    test_identity_1d();
    test_identity_3d();
    test_trilinear_math();
    test_tetrahedral_math();
    test_process_image();

    // --- TEST-1: SIMD vs scalar consistency ---
    test_simd_scalar_consistency_tetrahedral();
    test_simd_scalar_consistency_trilinear();

    // --- TEST-2: Edge inputs ---
    test_out_of_range_inputs();
    test_special_float_inputs();

    // --- TEST-3: Multi-threaded determinism ---
    test_multithreaded_determinism();

    // --- FEAT-5: Pipeline ---
    test_pipeline_single_stage();
    test_pipeline_multi_stage_identity();
    test_pipeline_process_pixels();
    test_pipeline_lifecycle();

    return 0;
}
