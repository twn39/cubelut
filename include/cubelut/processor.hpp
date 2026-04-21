#pragma once

#include <array>
#include <cstdint>
#include "lut.hpp"

namespace cubelut {

enum class Interpolation {
    Trilinear,
    Tetrahedral
};

class Processor {
public:
    // Apply LUT to a single pixel (RGB in range [0, 1])
    static std::array<float, 3> process(const Lut& lut, const std::array<float, 3>& pixel, Interpolation interp = Interpolation::Tetrahedral);

    // Apply LUT to a specified chunk of an image in-place (RGB format, floats)
    // startIndex and endIndex are in PIXEL count (not float count).
    // E.g., for the whole image, use 0 and width * height.
    static void processPixels(const Lut& lut, float* data, size_t startIndex, size_t endIndex, Interpolation interp = Interpolation::Tetrahedral);

    // Apply LUT to an entire image in-place (RGB format, floats). Helper wrapper.
    static void processImage(const Lut& lut, float* data, size_t width, size_t height, Interpolation interp = Interpolation::Tetrahedral) {
        processPixels(lut, data, 0, width * height, interp);
    }

    // SIMD-accelerated RGB float32 → RGBA float32 packing (alpha channel = 1.0f).
    // `rgba` must point to a buffer of at least numPixels * 4 floats.
    static void convertRGBToRGBA32(const float* rgb, float* rgba, size_t numPixels);

    // SIMD-accelerated RGB float32 → RGBA float16 packing (alpha = 0x3C00 = 1.0f in IEEE 754 half).
    // `rgba16` must point to a buffer of at least numPixels * 4 uint16_t values.
    static void convertRGBToRGBA16(const float* rgb, uint16_t* rgba16, size_t numPixels);

private:
    static std::array<float, 3> process1D(const LutData1D& lut, const std::array<float, 3>& pixel);
    static std::array<float, 3> process3DTrilinear(const LutData3D& lut, const std::array<float, 3>& pixel);
    static std::array<float, 3> process3DTetrahedral(const LutData3D& lut, const std::array<float, 3>& pixel);
};

} // namespace cubelut
