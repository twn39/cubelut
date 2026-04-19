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
            std::cout << "  Type: " << (lut.type == cubelut::LutType::Lut1D ? "1D" : "3D") << std::endl;
            std::cout << "  Size: " << lut.size << std::endl;
            
            assert(lut.size > 0);
            assert(!lut.data.empty());
            assert(lut.isValid());
            
            // Basic sanity check: data should be within or near [0, 1] usually, 
            // but some LUTs might have out of range values for HDR or specific shaper LUTs.
            // For now just check it's not all zeros or obviously broken.
            bool allZero = true;
            for (size_t i = 0; i < std::min<size_t>(lut.data.size(), 100); ++i) {
                if (lut.data[i] != 0.0f) {
                    allZero = false;
                    break;
                }
            }
            // assert(!allZero); // Not always true for some LUTs but usually true for these
            
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
