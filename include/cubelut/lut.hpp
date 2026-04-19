#pragma once

#include <vector>
#include <string>
#include <array>

namespace cubelut {

enum class LutType {
    Lut1D,
    Lut3D
};

struct Lut {
    LutType type = LutType::Lut3D;
    std::string title;
    
    // Domain limits (often [0, 1])
    std::array<float, 3> domainMin = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> domainMax = {1.0f, 1.0f, 1.0f};
    
    // Size: 1D size or 3D dimension (e.g. 33 means 33x33x33)
    // Adobe .cube format typically uses cubic 3D LUTs (size x size x size)
    int size = 0; 
    
    // Data in RGB format
    // For 1D LUT: size * 3 floats
    // For 3D LUT: size * size * size * 3 floats
    std::vector<float> data;

    bool isValid() const {
        if (size <= 0) return false;
        size_t expectedSize = 0;
        if (type == LutType::Lut1D) {
            expectedSize = static_cast<size_t>(size) * 3;
        } else {
            expectedSize = static_cast<size_t>(size) * size * size * 3;
        }
        return data.size() == expectedSize;
    }
};

} // namespace cubelut
