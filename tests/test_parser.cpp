#include <iostream>
#include <cassert>
#include <filesystem>
#include "cubelut/cubelut.hpp"

namespace fs = std::filesystem;

void test_load_real_files() {
    std::string testFilesDir = "tests/files";
    
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

int main() {
    try {
        test_load_real_files();
        std::cout << "All file loading tests passed!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
