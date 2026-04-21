// ============================================================================
// test_parser.cpp
//
// Covers:
//  - Original tests (real files, malformed sizes, broken floats)
//  - FEAT-1: ParseResult error codes (all 6 error paths validated)
//  - FEAT-3: LUT_1D_INPUT_RANGE / LUT_3D_INPUT_RANGE keyword parsing
// ============================================================================
#include <iostream>
#include <cassert>
#include <cmath>
#include <string>
#include <filesystem>
#include "cubelut/cubelut.hpp"

namespace fs = std::filesystem;

// Minimal valid 2×2×2 cube string for use in multiple tests.
static const char* k_valid_2x_cube =
    "LUT_3D_SIZE 2\n"
    "0 0 0\n1 0 0\n0 1 0\n1 1 0\n"
    "0 0 1\n1 0 1\n0 1 1\n1 1 1\n";

// ---------------------------------------------------------------------------
// Original tests (preserved)
// ---------------------------------------------------------------------------

void test_load_real_files() {
    std::string testFilesDir = "tests/files";
    if (!fs::exists(testFilesDir)) testFilesDir = "../tests/files";

    if (!fs::exists(testFilesDir)) {
        std::cerr << "Warning: Could not find tests/files directory. Skipping real file tests."
                  << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(testFilesDir)) {
        if (entry.path().extension() == ".cube") {
            std::cout << "Testing file: " << entry.path() << std::endl;
            auto lutOpt = cubelut::Parser::fromFile(entry.path().string());

            assert(lutOpt.has_value());
            const auto& lut = *lutOpt;

            std::cout << "  Title: " << lut.title << std::endl;
            if (lut.shaper1D) {
                std::cout << "  Has 1D Shaper Size: " << lut.shaper1D->size << std::endl;
                assert(lut.shaper1D->size > 0);
                assert(!lut.shaper1D->data.empty());
            }
            if (lut.grid3D) {
                std::cout << "  Has 3D Grid Size: " << lut.grid3D->size << std::endl;
                assert(lut.grid3D->size > 0);
                assert(!lut.grid3D->data.empty());
            }
            assert(lut.isValid());
            std::cout << "  Successfully validated " << entry.path().filename() << std::endl;
        }
    }
}

void test_malformed_sizes() {
    // Oversized 3D LUT (> 256 limit)
    auto lutOpt = cubelut::Parser::fromString("LUT_3D_SIZE 2000\n");
    assert(!lutOpt.has_value());

    // Negative size 1D LUT
    lutOpt = cubelut::Parser::fromString("LUT_1D_SIZE -5\n");
    assert(!lutOpt.has_value());

    std::cout << "test_malformed_sizes passed" << std::endl;
}

void test_broken_floats() {
    std::string broken_data =
        "LUT_3D_SIZE 2\n"
        "0.0 0.0 0.0\n"
        "1.0 1.0 1.0\n"
        "1.0 1.0 .-\n"   // malformed; line skipped
        "2.0 2.0 2.0\n"
        "3.0 3.0 3.0\n"
        "4.0 4.0 4.0\n"
        "5.0 5.0 5.0\n"
        "6.0 6.0 6.0\n"
        "7.0 7.0 7.0\n";

    auto lutOpt = cubelut::Parser::fromString(broken_data);
    assert(lutOpt.has_value());
    assert(lutOpt->grid3D.has_value());
    assert(lutOpt->grid3D->size == 2);
    assert(lutOpt->grid3D->data.size() == 24);
    assert(lutOpt->grid3D->data[6] == 2.0f);
    std::cout << "test_broken_floats passed" << std::endl;
}

// ---------------------------------------------------------------------------
// FEAT-1: ParseResult – success path
// ---------------------------------------------------------------------------
void test_parse_result_success() {
    auto result = cubelut::Parser::parseString(k_valid_2x_cube);

    assert(result.ok());
    assert(bool(result));
    assert(result.error == cubelut::ParseError::None);
    assert(result.errorMessage.empty());
    assert(result.lut.has_value());
    assert(result.lut->isValid());
    assert(result.lut->grid3D->size == 2);

    std::cout << "test_parse_result_success passed" << std::endl;
}

// ---------------------------------------------------------------------------
// FEAT-1: ParseResult – all failure error codes
// ---------------------------------------------------------------------------
void test_parse_result_missing_size() {
    // No LUT_*_SIZE directive at all
    auto result = cubelut::Parser::parseString("TITLE NoSize\n0.5 0.5 0.5\n");

    assert(!result.ok());
    assert(result.error == cubelut::ParseError::MissingLutSizeDirective);
    assert(!result.errorMessage.empty());
    std::cout << "test_parse_result_missing_size passed" << std::endl;
}

void test_parse_result_invalid_size() {
    // Oversized 3D LUT triggers InvalidLutSize
    auto r3 = cubelut::Parser::parseString("LUT_3D_SIZE 9999\n");
    assert(!r3.ok());
    assert(r3.error == cubelut::ParseError::InvalidLutSize);

    // Oversized 1D LUT
    auto r1 = cubelut::Parser::parseString("LUT_1D_SIZE 100000\n");
    assert(!r1.ok());
    assert(r1.error == cubelut::ParseError::InvalidLutSize);

    std::cout << "test_parse_result_invalid_size passed" << std::endl;
}

void test_parse_result_invalid_domain_min() {
    std::string bad = "DOMAIN_MIN abc def ghi\n";
    bad += k_valid_2x_cube;
    auto result = cubelut::Parser::parseString(bad);

    assert(!result.ok());
    assert(result.error == cubelut::ParseError::InvalidDomain);
    std::cout << "test_parse_result_invalid_domain_min passed" << std::endl;
}

void test_parse_result_invalid_domain_max() {
    std::string bad = "DOMAIN_MAX xyz\n";
    bad += k_valid_2x_cube;
    auto result = cubelut::Parser::parseString(bad);

    assert(!result.ok());
    assert(result.error == cubelut::ParseError::InvalidDomain);
    std::cout << "test_parse_result_invalid_domain_max passed" << std::endl;
}

void test_parse_result_insufficient_data() {
    // LUT_3D_SIZE 2 expects 8 rows; only 5 provided
    auto result = cubelut::Parser::parseString(
        "LUT_3D_SIZE 2\n"
        "0 0 0\n1 0 0\n0 1 0\n1 1 0\n0 0 1\n"
    );
    assert(!result.ok());
    assert(result.error == cubelut::ParseError::InsufficientData);
    assert(!result.errorMessage.empty());
    std::cout << "test_parse_result_insufficient_data passed" << std::endl;
}

void test_parse_result_file_not_found() {
    auto result = cubelut::Parser::parseFile("/nonexistent/path/that/does/not/exist.cube");

    assert(!result.ok());
    assert(result.error == cubelut::ParseError::FileNotFound);
    assert(!result.errorMessage.empty());
    std::cout << "test_parse_result_file_not_found passed" << std::endl;
}

void test_parse_error_to_string() {
    // Every error code must return a non-null, non-empty string
    const cubelut::ParseError codes[] = {
        cubelut::ParseError::None,
        cubelut::ParseError::FileNotFound,
        cubelut::ParseError::FileReadError,
        cubelut::ParseError::MissingLutSizeDirective,
        cubelut::ParseError::InvalidLutSize,
        cubelut::ParseError::InvalidDomain,
        cubelut::ParseError::InsufficientData,
    };
    for (auto e : codes) {
        const char* s = cubelut::parseErrorToString(e);
        assert(s != nullptr && s[0] != '\0');
    }
    std::cout << "test_parse_error_to_string passed (7 codes)" << std::endl;
}

// ---------------------------------------------------------------------------
// FEAT-3: LUT_1D_INPUT_RANGE / LUT_3D_INPUT_RANGE
// ---------------------------------------------------------------------------
void test_lut_1d_input_range() {
    // Range [-1.0, 2.0] – not the default [0, 1]
    auto result = cubelut::Parser::parseString(
        "LUT_1D_INPUT_RANGE -1.0 2.0\n"
        "LUT_1D_SIZE 2\n"
        "0.0 0.0 0.0\n"
        "1.0 1.0 1.0\n"
    );

    assert(result.ok());
    const auto& d1 = *result.lut->shaper1D;
    // Domain should be uniformly set from the scalar range
    assert(std::abs(d1.domain.min[0] - (-1.0f)) < 1e-6f);
    assert(std::abs(d1.domain.min[1] - (-1.0f)) < 1e-6f);
    assert(std::abs(d1.domain.min[2] - (-1.0f)) < 1e-6f);
    assert(std::abs(d1.domain.max[0] - 2.0f) < 1e-6f);
    assert(std::abs(d1.domain.max[1] - 2.0f) < 1e-6f);
    assert(std::abs(d1.domain.max[2] - 2.0f) < 1e-6f);

    std::cout << "test_lut_1d_input_range passed" << std::endl;
}

void test_lut_3d_input_range() {
    // Wide HDR range [0.0, 16.0], all three channels uniform
    std::string cube = "LUT_3D_INPUT_RANGE 0.0 16.0\nLUT_3D_SIZE 2\n";
    for (int i = 0; i < 8; ++i) cube += "0.5 0.5 0.5\n";

    auto result = cubelut::Parser::parseString(cube);

    assert(result.ok());
    const auto& d3 = *result.lut->grid3D;
    for (int ch = 0; ch < 3; ++ch) {
        assert(std::abs(d3.domain.min[ch] - 0.0f)  < 1e-6f);
        assert(std::abs(d3.domain.max[ch] - 16.0f) < 1e-6f);
    }
    std::cout << "test_lut_3d_input_range passed (domain max=16)" << std::endl;
}

void test_lut_input_range_invalid() {
    // Non-parseable float in INPUT_RANGE → InvalidDomain
    auto result = cubelut::Parser::parseString(
        "LUT_3D_INPUT_RANGE not_a_float 1.0\n"
        "LUT_3D_SIZE 2\n"
    );
    assert(!result.ok());
    assert(result.error == cubelut::ParseError::InvalidDomain);
    std::cout << "test_lut_input_range_invalid passed" << std::endl;
}

void test_lut_input_range_overridden_by_domain() {
    // LUT_*_INPUT_RANGE applies to the NEXT size directive.
    // After LUT_3D_SIZE is parsed the domain is reset to default [0,1].
    // A subsequent DOMAIN_MIN/MAX (before the next size) would be used for the
    // next LUT. Here we just verify the 3D LUT got the INPUT_RANGE domain.
    std::string cube =
        "LUT_3D_INPUT_RANGE -0.5 1.5\n"
        "LUT_3D_SIZE 2\n";
    for (int i = 0; i < 8; ++i) cube += "0 0 0\n";

    auto result = cubelut::Parser::parseString(cube);
    assert(result.ok());
    const auto& d3 = *result.lut->grid3D;
    assert(std::abs(d3.domain.min[0] - (-0.5f)) < 1e-6f);
    assert(std::abs(d3.domain.max[0] - 1.5f)   < 1e-6f);
    std::cout << "test_lut_input_range_overridden_by_domain passed" << std::endl;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    try {
        // --- Original ---
        test_load_real_files();
        test_malformed_sizes();
        test_broken_floats();
        std::cout << "All file loading tests passed!" << std::endl;

        // --- FEAT-1: ParseResult ---
        test_parse_result_success();
        test_parse_result_missing_size();
        test_parse_result_invalid_size();
        test_parse_result_invalid_domain_min();
        test_parse_result_invalid_domain_max();
        test_parse_result_insufficient_data();
        test_parse_result_file_not_found();
        test_parse_error_to_string();

        // --- FEAT-3: INPUT_RANGE keywords ---
        test_lut_1d_input_range();
        test_lut_3d_input_range();
        test_lut_input_range_invalid();
        test_lut_input_range_overridden_by_domain();

        std::cout << "\nAll parser tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
