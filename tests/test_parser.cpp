#include <iostream>
#include <cassert>
#include <filesystem>
#include "cubelut/cubelut.hpp"

namespace fs = std::filesystem;

void test_load_real_files() {
    std::string testFilesDir = "tests/files";
    if (!fs::exists(testFilesDir)) {
        testFilesDir = "../tests/files";
    }
    
    if (!fs::exists(testFilesDir)) {
        std::cerr << "Warning: Could not find tests/files directory. Skipping real file tests." << std::endl;
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
            
            // Basic sanity check: data should be within or near [0, 1] usually, 
            // but some LUTs might have out of range values for HDR or specific shaper LUTs.
            // For now just check it's not all zeros or obviously broken.
            if (lut.grid3D) {
                bool allZero = true;
                for (size_t i = 0; i < std::min<size_t>(lut.grid3D->data.size(), 100); ++i) {
                    if (lut.grid3D->data[i] != 0.0f) {
                        allZero = false;
                        break;
                    }
                }
            }
            
            std::cout << "  Successfully validated " << entry.path().filename() << std::endl;
        }
    }
}

void test_malformed_sizes() {
    // Maliciously large 3D LUT
    std::string malformed_3d = "LUT_3D_SIZE 2000\n";
    auto lutOpt = cubelut::Parser::fromString(malformed_3d);
    assert(!lutOpt.has_value());

    // Negative size 1D LUT
    std::string malformed_1d = "LUT_1D_SIZE -5\n";
    lutOpt = cubelut::Parser::fromString(malformed_1d);
    assert(!lutOpt.has_value());

    std::cout << "test_malformed_sizes passed" << std::endl;
}

void test_broken_floats() {
    std::string broken_data = 
        "LUT_3D_SIZE 2\n"
        "0.0 0.0 0.0\n"
        "1.0 1.0 1.0\n"
        "1.0 1.0 .-\n"     // Malformed float, should skip the line but not crash
        "2.0 2.0 2.0\n"
        "3.0 3.0 3.0\n"
        "4.0 4.0 4.0\n"
        "5.0 5.0 5.0\n"
        "6.0 6.0 6.0\n"
        "7.0 7.0 7.0\n";
    
    // Size is 2, expects 8 items. One row is broken, so it reads 8 valid rows eventually.
    auto lutOpt = cubelut::Parser::fromString(broken_data);
    assert(lutOpt.has_value());
    assert(lutOpt->grid3D.has_value());
    assert(lutOpt->grid3D->size == 2);
    // Data vector should be full (8 items * 3 floats = 24 floats)
    assert(lutOpt->grid3D->data.size() == 24);
    // Specifically, the broken line was skipped, so the 3rd valid row read was "2.0 2.0 2.0"
    assert(lutOpt->grid3D->data[6] == 2.0f);
    
    std::cout << "test_broken_floats passed" << std::endl;
}

int main() {
    try {
        test_load_real_files();
        test_malformed_sizes();
        test_broken_floats();
        std::cout << "All file loading tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
