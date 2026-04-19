#pragma once

#include <vector>
#include <string>
#include <array>
#include <optional>

namespace cubelut {

struct Domain {
    std::array<float, 3> min = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> max = {1.0f, 1.0f, 1.0f};
};

struct LutData1D {
    int size = 0;
    Domain domain;
    std::vector<float> data;

    bool isValid() const {
        return size > 0 && data.size() == static_cast<size_t>(size * 3);
    }
};

struct LutData3D {
    int size = 0;
    Domain domain;
    std::vector<float> data;

    bool isValid() const {
        return size > 0 && data.size() == static_cast<size_t>(size * size * size * 3);
    }
};

struct Lut {
    std::string title;
    
    std::optional<LutData1D> shaper1D;
    std::optional<LutData3D> grid3D;

    bool isValid() const {
        bool valid1D = shaper1D.has_value() && shaper1D->isValid();
        bool valid3D = grid3D.has_value() && grid3D->isValid();
        return valid1D || valid3D;
    }
};

} // namespace cubelut
