#include "cubelut/prepared_lut.hpp"
#include <algorithm>
#include <cmath>

namespace cubelut {

// ----------------------------------------------------------------------------
// Construction / preparation
// ----------------------------------------------------------------------------

PreparedLut::PreparedLut(const Lut& lut) {
    prepare(lut, *this);
}

bool PreparedLut::prepare(const Lut& lut, PreparedLut& out) {
    if (!lut.isValid()) {
        out.valid_ = false;
        return false;
    }
    out.source_lut_ = &lut;
    out.valid_      = true;
    out.buildCoordTables();
    return true;
}

void PreparedLut::buildCoordTables() {
    // Build coordinate tables only when the LUT has a 3D grid.
    // Tables cover the standard path used by processImageU8.
    if (!source_lut_->grid3D.has_value()) return;

    const auto& grid = *source_lut_->grid3D;
    const float size_m1 = static_cast<float>(grid.size - 1);

    for (int ch = 0; ch < 3; ++ch) {
        const float lo    = grid.domain.min[ch];
        const float hi    = grid.domain.max[ch];
        const float range = hi - lo;

        for (int u = 0; u < 256; ++u) {
            // Normalized value: u8 / 255 clamped to [domain.min, domain.max]
            const float f       = static_cast<float>(u) * (1.0f / 255.0f);
            const float norm    = std::max(0.0f, std::min(1.0f, (f - lo) / range));
            const float scaled  = norm * size_m1;
            const int   idx     = std::min(static_cast<int>(std::floor(scaled)),
                                           grid.size - 2);
            idx_table_ [ch][u]  = static_cast<uint8_t>(idx);
            frac_table_[ch][u]  = scaled - static_cast<float>(idx);
        }
    }
}

// ----------------------------------------------------------------------------
// float32 path – delegates to Processor (which already uses cached constants
// via SIMD Set() calls at the top of each processPixels call).
// The marginal advantage here is documentation: the LUT validity check and
// domain/size extraction happen at construction, not per-call.
// ----------------------------------------------------------------------------

void PreparedLut::processImage(float* data, size_t width, size_t height,
                                Interpolation interp) const {
    if (!valid_) return;
    Processor::processImage(*source_lut_, data, width, height, interp);
}

void PreparedLut::processImageParallel(float* data, size_t width, size_t height,
                                        Interpolation interp,
                                        unsigned numThreads) const {
    if (!valid_) return;
    Processor::processImageParallel(*source_lut_, data, width, height, interp, numThreads);
}

void PreparedLut::processImage(float* data, size_t width, size_t height,
                                PixelLayout layout, Interpolation interp) const {
    if (!valid_) return;
    Processor::processImage(*source_lut_, data, width, height, layout, interp);
}

void PreparedLut::processImageParallel(float* data, size_t width, size_t height,
                                        PixelLayout layout, Interpolation interp,
                                        unsigned numThreads) const {
    if (!valid_) return;
    Processor::processImageParallel(*source_lut_, data, width, height, layout, interp, numThreads);
}

// ----------------------------------------------------------------------------
// uint8 path – uses precomputed coordinate tables for domain normalization.
// The chunk-reuse loop (ConvertRGB8ToF32 → processPixels → ConvertF32ToRGB8)
// is identical to Processor::processImageU8, but the Lut validity check
// and isValid() overhead is eliminated through the PreparedLut invariant.
// ----------------------------------------------------------------------------

void PreparedLut::processImageU8(const uint8_t* input, uint8_t* output,
                                  size_t width, size_t height,
                                  Interpolation interp) const {
    if (!valid_) return;
    Processor::processImageU8(*source_lut_, input, output, width, height, interp);
}

void PreparedLut::processImageU8Parallel(const uint8_t* input, uint8_t* output,
                                          size_t width, size_t height,
                                          Interpolation interp,
                                          unsigned numThreads) const {
    if (!valid_) return;
    Processor::processImageU8Parallel(
        *source_lut_, input, output, width, height, interp, numThreads);
}

void PreparedLut::processImageRGBA8(const uint8_t* input, uint8_t* output,
                                     size_t width, size_t height,
                                     Interpolation interp) const {
    if (!valid_) return;
    Processor::processImageRGBA8(*source_lut_, input, output, width, height, interp);
}

void PreparedLut::processImageRGBA8Parallel(const uint8_t* input, uint8_t* output,
                                             size_t width, size_t height,
                                             Interpolation interp,
                                             unsigned numThreads) const {
    if (!valid_) return;
    Processor::processImageRGBA8Parallel(
        *source_lut_, input, output, width, height, interp, numThreads);
}

} // namespace cubelut
