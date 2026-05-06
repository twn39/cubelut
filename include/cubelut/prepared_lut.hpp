#pragma once

#include <array>
#include <cstdint>
#include "lut.hpp"
#include "processor.hpp"

namespace cubelut {

// ----------------------------------------------------------------------------
// PreparedLut – a stateful, pre-compiled LUT executor.
//
// Motivation (OCIO CPUProcessor pattern):
//   Processor::processImage() recomputes SIMD broadcast constants (scale,
//   offset) on every call from lut.domain and lut.size. For video pipelines
//   at 60 fps, these values are constant for the LUT's lifetime.
//
//   PreparedLut compiles those constants ONCE at construction time, then
//   reuses them across every frame – zero recomputation overhead.
//
//   For uint8 input, PreparedLut additionally pre-builds per-axis coordinate
//   tables (idx_table + frac_table, 3.75 KB total) that fit entirely in L1
//   data cache, replacing per-pixel float domain arithmetic with L1 lookups.
//
// Usage:
//   // Create once per LUT (≈ 10 µs):
//   cubelut::PreparedLut prepared(*Parser::fromFile("grade.cube"));
//
//   // Process many frames (zero constant recomputation):
//   for (auto& frame : frames)
//       prepared.processImageU8(frame.data, frame.data, W, H);
//
//   // Fall back to float32 path when needed:
//   prepared.processImage(float_buffer, W, H);
// ----------------------------------------------------------------------------

class PreparedLut {
public:
    // ── Construction ─────────────────────────────────────────────────────────

    /// Build the PreparedLut from a valid Lut.
    /// Precomputes:
    ///   - 6 SIMD broadcast constants (scale/offset per channel)
    ///   - 3 × 256 coord tables for uint8 input (3.75 KB → L1 hot)
    /// Returns false if the lut is invalid.
    static bool prepare(const Lut& lut, PreparedLut& out);

    /// Convenience: construct directly (throws no exception; check isValid()).
    explicit PreparedLut(const Lut& lut);

    PreparedLut() = default;

    bool isValid() const { return valid_; }

    // ── float32 path ─────────────────────────────────────────────────────────

    /// Apply LUT in-place on a float32 RGB image.
    /// Identical behaviour to Processor::processImage() but uses cached
    /// SIMD constants from construction – no per-call domain recomputation.
    void processImage(float* data, size_t width, size_t height,
                      Interpolation interp = Interpolation::Tetrahedral) const;

    void processImageParallel(float* data, size_t width, size_t height,
                              Interpolation interp     = Interpolation::Tetrahedral,
                              unsigned      numThreads = 0) const;

    // ── uint8 path ────────────────────────────────────────────────────────────

    /// Apply LUT to a uint8 RGB image (3 bytes/pixel).
    /// input == output is safe.
    void processImageU8(const uint8_t* input, uint8_t* output,
                        size_t width, size_t height,
                        Interpolation interp = Interpolation::Tetrahedral) const;

    void processImageU8Parallel(const uint8_t* input, uint8_t* output,
                                size_t width, size_t height,
                                Interpolation interp     = Interpolation::Tetrahedral,
                                unsigned      numThreads = 0) const;

    /// Apply LUT to a uint8 RGBA image (4 bytes/pixel, alpha passthrough).
    void processImageRGBA8(const uint8_t* input, uint8_t* output,
                           size_t width, size_t height,
                           Interpolation interp = Interpolation::Tetrahedral) const;

    void processImageRGBA8Parallel(const uint8_t* input, uint8_t* output,
                                   size_t width, size_t height,
                                   Interpolation interp     = Interpolation::Tetrahedral,
                                   unsigned      numThreads = 0) const;

    // ── Inspection ────────────────────────────────────────────────────────────

    const Lut* sourceLut() const { return source_lut_; }

private:
    void buildCoordTables();

    const Lut*  source_lut_ = nullptr; ///< Non-owning reference to source Lut.
    bool        valid_      = false;

    // Precomputed per-axis coordinate tables for uint8 input (total: 3.75 KB).
    // For each uint8 value (0-255) and each channel axis (R/G/B):
    //   idx_table_[ch][u8]  = LUT grid index (0 .. size-2)
    //   frac_table_[ch][u8] = fractional part within the cell (0.0 .. 1.0)
    // These fit entirely in L1 data cache (64 KB), remaining hot across frames.
    uint8_t idx_table_ [3][256] = {};
    float   frac_table_[3][256] = {};
};

} // namespace cubelut
