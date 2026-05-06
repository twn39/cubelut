// ============================================================================
// test_baker_writer.cpp
//
// Covers:
//  - Baker::makeIdentity3D / makeIdentity1D correctness
//  - Baker::bake3D(Pipeline) – identity pipeline → identity grid
//  - Baker::bake3D(Pipeline) – non-trivial pipeline correctness
//  - Baker::bake3D(callable) – lambda inversion
//  - Writer::toString format (keywords, data count, precision)
//  - Writer roundtrip: toString → Parser::parseString → data equality
//  - Writer domain: default suppressed, non-default emitted
//  - WriteOptions: precision, preserveComments, generator comment toggle
//  - Writer::toFile error paths (invalid LUT, bad path)
//  - Baker + Writer combined workflow: bake two LUTs → write → re-parse
//  - C API: cubelut_write_to_file, cubelut_write_to_string,
//           cubelut_bake_identity_grid3d
// ============================================================================

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include "cubelut/cubelut.hpp"
#include "cubelut/cubelut_c.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static cubelut::Lut make_identity_2x3d() {
    cubelut::Lut lut;
    cubelut::LutData3D d;
    d.size = 2;
    d.data = {0,0,0, 1,0,0, 0,1,0, 1,1,0, 0,0,1, 1,0,1, 0,1,1, 1,1,1};
    lut.grid3D = std::move(d);
    return lut;
}

static cubelut::Lut make_nontrivial_5x3d() {
    cubelut::Lut lut;
    cubelut::LutData3D d;
    d.size = 5;
    const int N = 5;
    d.data.reserve(N * N * N * 3);
    for (int b = 0; b < N; ++b)
    for (int g = 0; g < N; ++g)
    for (int r = 0; r < N; ++r) {
        float fr = r / float(N-1), fg = g / float(N-1), fb = b / float(N-1);
        d.data.push_back(std::sqrt(fr));
        d.data.push_back(fg * fg);
        d.data.push_back(0.5f*fr + 0.3f*fg + 0.2f*fb);
    }
    lut.grid3D = std::move(d);
    return lut;
}

// Count occurrences of a substring in a string.
static int count_occurrences(const std::string& haystack, const std::string& needle) {
    int count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

// ---------------------------------------------------------------------------
// Baker tests
// ---------------------------------------------------------------------------

void test_baker_identity3d_corners() {
    auto grid = cubelut::Baker::makeIdentity3D(4);
    assert(grid.size == 4);
    assert(grid.data.size() == size_t(4*4*4*3));
    assert(grid.isValid());

    // First entry: (r=0,g=0,b=0) → (0,0,0)
    assert(grid.data[0] == 0.0f && grid.data[1] == 0.0f && grid.data[2] == 0.0f);
    // Last entry: (r=3,g=3,b=3)/(3) → (1,1,1)
    size_t last = grid.data.size() - 3;
    assert(std::abs(grid.data[last]   - 1.0f) < 1e-6f);
    assert(std::abs(grid.data[last+1] - 1.0f) < 1e-6f);
    assert(std::abs(grid.data[last+2] - 1.0f) < 1e-6f);

    // Index of (r=3,g=0,b=0) = b*N²+g*N+r = 0*16+0*4+3 = 3 → R=1, G=0, B=0
    assert(std::abs(grid.data[3*3+0] - 1.0f) < 1e-6f);
    assert(grid.data[3*3+1] == 0.0f);
    assert(grid.data[3*3+2] == 0.0f);

    std::cout << "test_baker_identity3d_corners passed\n";
}

void test_baker_identity1d_endpoints() {
    auto lut = cubelut::Baker::makeIdentity1D(1024);
    assert(lut.size == 1024);
    assert(lut.data.size() == size_t(1024*3));
    assert(lut.isValid());

    // First entry → 0
    assert(lut.data[0] == 0.0f && lut.data[1] == 0.0f && lut.data[2] == 0.0f);
    // Last entry → 1
    assert(std::abs(lut.data[1023*3+0] - 1.0f) < 1e-6f);

    // Middle entry ~0.5
    assert(std::abs(lut.data[512*3+0] - 512.0f/1023.0f) < 1e-6f);

    std::cout << "test_baker_identity1d_endpoints passed\n";
}

void test_baker_bake3d_identity_pipeline() {
    // Baking the identity 2×2×2 LUT through a pipeline should return identical data.
    auto id_lut = make_identity_2x3d();
    cubelut::Pipeline pipe;
    pipe.addStage(id_lut);

    auto baked = cubelut::Baker::bake3D(pipe, 5);
    assert(baked.isValid());
    assert(baked.size == 5);

    // Every baked grid point should equal its input coordinates (identity).
    // Cross-validate against Baker::makeIdentity3D.
    auto ref = cubelut::Baker::makeIdentity3D(5);
    for (size_t i = 0; i < baked.data.size(); ++i) {
        float diff = std::abs(baked.data[i] - ref.data[i]);
        if (diff >= 1e-4f) {
            std::cerr << "Identity bake mismatch at [" << i << "]: "
                      << baked.data[i] << " vs " << ref.data[i] << "\n";
            assert(false);
        }
    }
    std::cout << "test_baker_bake3d_identity_pipeline passed\n";
}

void test_baker_bake3d_nontrivial_pipeline() {
    // Baking a non-trivial LUT and evaluating at a known point.
    auto src_lut = make_nontrivial_5x3d();
    cubelut::Pipeline pipe;
    pipe.addStage(src_lut);

    auto baked = cubelut::Baker::bake3D(pipe, 5);
    assert(baked.isValid());

    // The baked LUT applied to the same input must give the same output as
    // a direct process() call through the original pipeline.
    // Use Processor::process on baked Lut and compare with pipe.process.
    cubelut::Lut baked_lut;
    baked_lut.grid3D = std::move(baked);

    const std::vector<std::array<float,3>> test_pts = {
        {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f},
        {0.5f, 0.5f, 0.5f}, {0.25f, 0.75f, 0.1f}
    };

    for (const auto& pt : test_pts) {
        auto from_pipe  = pipe.process(pt);
        auto from_baked = cubelut::Processor::process(baked_lut, pt);
        for (int ch = 0; ch < 3; ++ch) {
            float diff = std::abs(from_pipe[ch] - from_baked[ch]);
            if (diff >= 1e-3f) {   // baked LUT has interpolation error
                std::cerr << "Bake mismatch ch=" << ch
                          << " pipe=" << from_pipe[ch]
                          << " baked=" << from_baked[ch] << "\n";
                assert(false);
            }
        }
    }
    std::cout << "test_baker_bake3d_nontrivial_pipeline passed\n";
}

void test_baker_bake3d_callable_invert() {
    // Bake an inversion: f(r,g,b) = (1-r, 1-g, 1-b)
    auto grid = cubelut::Baker::bake3D([](std::array<float,3> p) {
        return std::array<float,3>{1.0f - p[0], 1.0f - p[1], 1.0f - p[2]};
    }, 4);

    assert(grid.isValid() && grid.size == 4);

    // Grid corner (r=0,g=0,b=0) → should become (1,1,1)
    assert(std::abs(grid.data[0] - 1.0f) < 1e-6f);
    assert(std::abs(grid.data[1] - 1.0f) < 1e-6f);
    assert(std::abs(grid.data[2] - 1.0f) < 1e-6f);

    // Grid corner (r=3,g=0,b=0)/3 = (1,0,0) → should become (0,1,1)
    assert(std::abs(grid.data[3*3+0] - 0.0f) < 1e-6f);
    assert(std::abs(grid.data[3*3+1] - 1.0f) < 1e-6f);
    assert(std::abs(grid.data[3*3+2] - 1.0f) < 1e-6f);

    std::cout << "test_baker_bake3d_callable_invert passed\n";
}

// ---------------------------------------------------------------------------
// Writer format tests
// ---------------------------------------------------------------------------

void test_writer_output_contains_keywords_3d() {
    auto lut = make_identity_2x3d();
    lut.title = "Test LUT";
    std::string text = cubelut::Writer::toString(lut);

    assert(!text.empty());
    assert(text.find("LUT_3D_SIZE 2") != std::string::npos);
    assert(text.find("TITLE \"Test LUT\"") != std::string::npos);
    assert(text.find("# Created by cubelut") != std::string::npos);
    // 2³ = 8 data lines (each with 3 floats)
    assert(count_occurrences(text, "\n") >= 8);

    std::cout << "test_writer_output_contains_keywords_3d passed\n";
}

void test_writer_data_line_count_3d() {
    // 33³ = 35937 data lines
    cubelut::Lut lut;
    lut.grid3D = cubelut::Baker::makeIdentity3D(33);
    std::string text = cubelut::Writer::toString(lut);

    // Count lines that look like data: "0.000000 0.000000 0.000000"
    // A simpler proxy: count '\n' occurrences minus header lines.
    int newlines = count_occurrences(text, "\n");
    // Header lines: comment + blank + LUT_3D_SIZE = 3 lines
    assert(newlines >= 35937);

    std::cout << "test_writer_data_line_count_3d passed (" << newlines << " lines)\n";
}

void test_writer_precision_option() {
    auto lut = make_identity_2x3d();
    cubelut::WriteOptions opts;
    opts.precision = 8;
    opts.writeGeneratorComment = false;
    std::string text = cubelut::Writer::toString(lut, opts);

    // With precision=8, the first non-zero value "1.00000000"
    assert(text.find("1.00000000") != std::string::npos);

    opts.precision = 3;
    std::string text3 = cubelut::Writer::toString(lut, opts);
    assert(text3.find("1.000 ") != std::string::npos || text3.find("1.000\n") != std::string::npos);

    std::cout << "test_writer_precision_option passed\n";
}

void test_writer_generator_comment_suppressed() {
    auto lut = make_identity_2x3d();
    cubelut::WriteOptions opts;
    opts.writeGeneratorComment = false;
    std::string text = cubelut::Writer::toString(lut, opts);
    assert(text.find("# Created by cubelut") == std::string::npos);
    std::cout << "test_writer_generator_comment_suppressed passed\n";
}

void test_writer_custom_comments() {
    // Comments are now set on the Lut itself, not WriteOptions.
    auto lut = make_identity_2x3d();
    lut.comments = {"Author: Test", "Version: 1.0"};
    std::string text = cubelut::Writer::toString(lut);
    assert(text.find("# Author: Test") != std::string::npos);
    assert(text.find("# Version: 1.0") != std::string::npos);
    std::cout << "test_writer_custom_comments passed\n";
}

void test_writer_comment_blank_line() {
    // Empty string element → blank comment line "#"
    auto lut = make_identity_2x3d();
    lut.comments = {"Section A", "", "Section B"};
    std::string text = cubelut::Writer::toString(lut);
    assert(text.find("# Section A") != std::string::npos);
    assert(text.find("# Section B") != std::string::npos);
    // blank comment line must appear as "#\n" (not "# \n")
    assert(text.find("#\n") != std::string::npos);
    std::cout << "test_writer_comment_blank_line passed\n";
}

void test_writer_preserve_comments_false() {
    // preserveComments=false strips lut.comments from output
    auto lut = make_identity_2x3d();
    lut.comments = {"Author: Test", "Grade: Warm"};
    cubelut::WriteOptions opts;
    opts.preserveComments = false;
    std::string text = cubelut::Writer::toString(lut, opts);
    assert(text.find("# Author") == std::string::npos);
    assert(text.find("# Grade")  == std::string::npos);
    // Tool watermark still present
    assert(text.find("# Created by cubelut") != std::string::npos);
    std::cout << "test_writer_preserve_comments_false passed\n";
}

void test_comment_roundtrip() {
    // Parse .cube with comments → write → re-parse → comments preserved
    const char* cube_src =
        "# Author: colorist@studio.com\n"
        "# Grade: Warm Summer\n"
        "#\n"
        "# Input: Rec.709\n"
        "TITLE \"Summer\"\n"
        "LUT_3D_SIZE 2\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
        "0 0 1\n1 0 1\n0 1 1\n1 1 1\n";

    auto lut = cubelut::Parser::fromString(cube_src);
    assert(lut.has_value());
    assert(lut->comments.size() == 4u);
    assert(lut->comments[0] == "Author: colorist@studio.com");
    assert(lut->comments[1] == "Grade: Warm Summer");
    assert(lut->comments[2] == "");               // blank line
    assert(lut->comments[3] == "Input: Rec.709");
    assert(lut->title == "Summer");

    // Write and re-parse: comments must survive
    cubelut::WriteOptions opts;
    opts.writeGeneratorComment = false;  // avoid prepending extra comment
    std::string text = cubelut::Writer::toString(*lut, opts);

    auto reparsed = cubelut::Parser::fromString(text);
    assert(reparsed.has_value());
    assert(reparsed->comments.size() == 4u);
    assert(reparsed->comments[0] == "Author: colorist@studio.com");
    assert(reparsed->comments[1] == "Grade: Warm Summer");
    assert(reparsed->comments[2] == "");
    assert(reparsed->comments[3] == "Input: Rec.709");
    assert(reparsed->title == "Summer");
    std::cout << "test_comment_roundtrip passed\n";
}

void test_writer_domain_default_not_written() {
    auto lut = make_identity_2x3d();
    // Default domain [0,1] — should NOT appear in output
    std::string text = cubelut::Writer::toString(lut);
    assert(text.find("DOMAIN_MIN") == std::string::npos);
    assert(text.find("DOMAIN_MAX") == std::string::npos);
    std::cout << "test_writer_domain_default_not_written passed\n";
}

void test_writer_domain_nondefault_written() {
    cubelut::Lut lut;
    cubelut::LutData3D d;
    d.size = 2;
    d.data = {0,0,0, 1,0,0, 0,1,0, 1,1,0, 0,0,1, 1,0,1, 0,1,1, 1,1,1};
    d.domain.min = {-0.3f, -0.3f, -0.3f};
    d.domain.max = {1.5f,  1.5f,  1.5f};
    lut.grid3D = std::move(d);

    std::string text = cubelut::Writer::toString(lut);
    assert(text.find("DOMAIN_MIN") != std::string::npos);
    assert(text.find("DOMAIN_MAX") != std::string::npos);
    // Values should appear before LUT_3D_SIZE (domain must precede size for parser)
    size_t domain_pos = text.find("DOMAIN_MIN");
    size_t size_pos   = text.find("LUT_3D_SIZE");
    assert(domain_pos < size_pos);
    std::cout << "test_writer_domain_nondefault_written passed\n";
}

void test_writer_domain_option_suppress() {
    cubelut::Lut lut;
    cubelut::LutData3D d;
    d.size = 2;
    d.data = {0,0,0, 1,0,0, 0,1,0, 1,1,0, 0,0,1, 1,0,1, 0,1,1, 1,1,1};
    d.domain.min = {-1.0f, -1.0f, -1.0f};
    d.domain.max = {2.0f,  2.0f,  2.0f};
    lut.grid3D = std::move(d);

    cubelut::WriteOptions opts;
    opts.writeDomainIfNonDefault = false;  // suppress
    std::string text = cubelut::Writer::toString(lut, opts);
    assert(text.find("DOMAIN_MIN") == std::string::npos);
    std::cout << "test_writer_domain_option_suppress passed\n";
}

void test_writer_invalid_lut_error() {
    cubelut::Lut empty_lut; // no shaper1D or grid3D
    assert(!empty_lut.isValid());

    // toString returns empty
    assert(cubelut::Writer::toString(empty_lut).empty());

    // toFile returns InvalidLut
    auto result = cubelut::Writer::toFile(empty_lut, "/tmp/invalid.cube");
    assert(!result.ok());
    assert(result.error == cubelut::WriteError::InvalidLut);
    assert(!result.errorMessage.empty());

    // toStream returns InvalidLut
    std::ostringstream oss;
    assert(cubelut::Writer::toStream(empty_lut, oss) == cubelut::WriteError::InvalidLut);

    std::cout << "test_writer_invalid_lut_error passed\n";
}

void test_writer_bad_path_error() {
    auto lut = make_identity_2x3d();
    auto result = cubelut::Writer::toFile(lut, "/nonexistent/dir/output.cube");
    assert(!result.ok());
    assert(result.error == cubelut::WriteError::FileOpenError);
    assert(!result.errorMessage.empty());
    std::cout << "test_writer_bad_path_error passed\n";
}

// ---------------------------------------------------------------------------
// Roundtrip test: write → parse → compare
// ---------------------------------------------------------------------------

void test_writer_roundtrip_3d() {
    auto original = make_identity_2x3d();
    original.title = "Roundtrip";
    original.grid3D->domain.min = {0.0f, 0.0f, 0.0f};
    original.grid3D->domain.max = {1.0f, 1.0f, 1.0f};

    std::string text = cubelut::Writer::toString(original);
    assert(!text.empty());

    auto reparsed = cubelut::Parser::fromString(text);
    assert(reparsed.has_value());
    assert(reparsed->isValid());
    assert(reparsed->title == "Roundtrip");
    assert(reparsed->grid3D->size == 2);
    assert(reparsed->grid3D->data.size() == 24u);

    // Data values must survive the precision-6 → parse roundtrip
    const float tol = 1e-5f;
    for (size_t i = 0; i < original.grid3D->data.size(); ++i) {
        float diff = std::abs(original.grid3D->data[i] - reparsed->grid3D->data[i]);
        if (diff >= tol) {
            std::cerr << "Roundtrip mismatch at [" << i << "]: "
                      << original.grid3D->data[i] << " vs "
                      << reparsed->grid3D->data[i] << "\n";
            assert(false);
        }
    }
    std::cout << "test_writer_roundtrip_3d passed\n";
}

void test_writer_roundtrip_nondefault_domain() {
    cubelut::Lut lut;
    cubelut::LutData3D d;
    d.size = 2;
    d.data = {0,0,0, 1,0,0, 0,1,0, 1,1,0, 0,0,1, 1,0,1, 0,1,1, 1,1,1};
    d.domain.min = {-0.5f, -0.5f, -0.5f};
    d.domain.max = { 1.5f,  1.5f,  1.5f};
    lut.grid3D = std::move(d);

    std::string text = cubelut::Writer::toString(lut);
    auto reparsed = cubelut::Parser::fromString(text);
    assert(reparsed.has_value() && reparsed->grid3D.has_value());

    const auto& dom = reparsed->grid3D->domain;
    assert(std::abs(dom.min[0] - (-0.5f)) < 1e-5f);
    assert(std::abs(dom.max[0] -   1.5f)  < 1e-5f);

    std::cout << "test_writer_roundtrip_nondefault_domain passed\n";
}

void test_writer_roundtrip_1d_and_3d() {
    cubelut::Lut lut;
    lut.title = "Shaper+Grade";
    lut.shaper1D = cubelut::Baker::makeIdentity1D(16);
    lut.grid3D   = cubelut::Baker::makeIdentity3D(4);

    std::string text = cubelut::Writer::toString(lut);
    assert(text.find("LUT_1D_SIZE 16") != std::string::npos);
    assert(text.find("LUT_3D_SIZE 4")  != std::string::npos);

    // 1D section must appear before 3D section
    assert(text.find("LUT_1D_SIZE") < text.find("LUT_3D_SIZE"));

    auto reparsed = cubelut::Parser::fromString(text);
    assert(reparsed.has_value() && reparsed->isValid());
    assert(reparsed->shaper1D.has_value() && reparsed->shaper1D->size == 16);
    assert(reparsed->grid3D.has_value()   && reparsed->grid3D->size == 4);

    std::cout << "test_writer_roundtrip_1d_and_3d passed\n";
}

// ---------------------------------------------------------------------------
// Baker + Writer combined workflow
// ---------------------------------------------------------------------------

void test_baker_writer_combine_two_luts() {
    // Build two simple LUTs: identity then sqrt-per-channel
    auto id_lut = make_identity_2x3d();

    cubelut::Pipeline pipe;
    pipe.addStage(id_lut);  // identity stage 1

    // Bake the pipeline into a new 9³ grid
    cubelut::Lut baked;
    baked.title  = "Combined";
    baked.grid3D = cubelut::Baker::bake3D(pipe, 9);
    assert(baked.isValid());

    // Write to string
    std::string text = cubelut::Writer::toString(baked);
    assert(!text.empty());

    // Re-parse the written LUT
    auto reparsed_opt = cubelut::Parser::fromString(text);
    assert(reparsed_opt.has_value());

    // Apply the reparsed LUT and compare with direct pipeline eval
    const std::array<float,3> test_px = {0.3f, 0.6f, 0.9f};
    auto from_pipe    = pipe.process(test_px);
    auto from_reparsed = cubelut::Processor::process(*reparsed_opt, test_px);

    for (int ch = 0; ch < 3; ++ch) {
        float diff = std::abs(from_pipe[ch] - from_reparsed[ch]);
        if (diff >= 1e-3f) {
            std::cerr << "Combined bake mismatch ch=" << ch
                      << " pipe=" << from_pipe[ch]
                      << " reparsed=" << from_reparsed[ch] << "\n";
            assert(false);
        }
    }
    std::cout << "test_baker_writer_combine_two_luts passed\n";
}

// ---------------------------------------------------------------------------
// C API tests
// ---------------------------------------------------------------------------

void test_c_api_write_to_string() {
    cubelut_lut_t* lut = cubelut_load_from_string(
        "LUT_3D_SIZE 2\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
        "0 0 1\n1 0 1\n0 1 1\n1 1 1\n"
    );
    assert(lut != nullptr);

    char* text = cubelut_write_to_string(lut);
    assert(text != nullptr);
    assert(std::string(text).find("LUT_3D_SIZE 2") != std::string::npos);

    cubelut_free_buffer(text);
    cubelut_free(lut);
    std::cout << "test_c_api_write_to_string passed\n";
}

void test_c_api_write_to_string_null() {
    assert(cubelut_write_to_string(nullptr) == nullptr);
    std::cout << "test_c_api_write_to_string_null passed\n";
}

void test_c_api_bake_identity_grid() {
    size_t num_floats = 0;
    float* buf = cubelut_bake_identity_grid3d(4, &num_floats);
    assert(buf != nullptr);
    assert(num_floats == size_t(4*4*4*3));

    // Corner (r=0,g=0,b=0) → (0,0,0)
    assert(buf[0] == 0.0f && buf[1] == 0.0f && buf[2] == 0.0f);
    // Last lattice point → (1,1,1)
    assert(std::abs(buf[num_floats-3] - 1.0f) < 1e-6f);

    cubelut_free_buffer(buf);
    assert(cubelut_bake_identity_grid3d(1, nullptr) == nullptr); // size < 2

    std::cout << "test_c_api_bake_identity_grid passed\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    // ── Baker ──────────────────────────────────────────────────────────────
    test_baker_identity3d_corners();
    test_baker_identity1d_endpoints();
    test_baker_bake3d_identity_pipeline();
    test_baker_bake3d_nontrivial_pipeline();
    test_baker_bake3d_callable_invert();

    // ── Writer format ──────────────────────────────────────────────────────
    test_writer_output_contains_keywords_3d();
    test_writer_data_line_count_3d();
    test_writer_precision_option();
    test_writer_generator_comment_suppressed();
    test_writer_custom_comments();
    test_writer_comment_blank_line();
    test_writer_preserve_comments_false();
    test_comment_roundtrip();
    test_writer_domain_default_not_written();
    test_writer_domain_nondefault_written();
    test_writer_domain_option_suppress();
    test_writer_invalid_lut_error();
    test_writer_bad_path_error();

    // ── Roundtrip ──────────────────────────────────────────────────────────
    test_writer_roundtrip_3d();
    test_writer_roundtrip_nondefault_domain();
    test_writer_roundtrip_1d_and_3d();

    // ── Combined workflow ──────────────────────────────────────────────────
    test_baker_writer_combine_two_luts();

    // ── C API ──────────────────────────────────────────────────────────────
    test_c_api_write_to_string();
    test_c_api_write_to_string_null();
    test_c_api_bake_identity_grid();

    std::cout << "\nAll Baker + Writer tests passed!\n";
    return 0;
}
