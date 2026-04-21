#pragma once

#include <vector>
#include <algorithm>
#include "lut.hpp"
#include "processor.hpp"

namespace cubelut {

// ---------------------------------------------------------------------------
// FEAT-5: Pipeline – composable, multi-stage LUT processing chain.
//
// A Pipeline chains one or more Lut stages in sequence, passing the output
// of each stage as the input to the next. This mirrors the colour management
// pipelines found in tools like ACES / OpenColorIO (Input → CDL → Output).
//
// Usage:
//   cubelut::Pipeline pipe;
//   pipe.addStage(*Parser::fromFile("log_to_linear.cube"));
//   pipe.addStage(*Parser::fromFile("film_emulation.cube"), Interpolation::Trilinear);
//
//   // Single pixel
//   auto out = pipe.process({r, g, b});
//
//   // Full image (multi-thread safe: each thread calls processPixels on a range)
//   pipe.processImage(pixels, width, height);
// ---------------------------------------------------------------------------

class Pipeline {
public:
    /// A single processing stage: one LUT + its interpolation mode.
    struct Stage {
        Lut           lut;
        Interpolation interp = Interpolation::Tetrahedral;
    };

    Pipeline() = default;

    // ------------------------------------------------------------------
    // Stage management
    // ------------------------------------------------------------------

    /// Append a LUT stage to the pipeline.
    void addStage(Lut lut, Interpolation interp = Interpolation::Tetrahedral);

    /// Remove all stages.
    void clear() noexcept { stages_.clear(); }

    /// Number of stages currently in the pipeline.
    size_t stageCount() const noexcept { return stages_.size(); }

    /// Read-only access to a stage (unchecked index).
    const Stage& stage(size_t idx) const { return stages_[idx]; }

    /// True if the pipeline has at least one valid stage.
    bool isValid() const noexcept {
        if (stages_.empty()) return false;
        for (const auto& s : stages_) {
            if (!s.lut.isValid()) return false;
        }
        return true;
    }

    // ------------------------------------------------------------------
    // Processing  –  all methods are const and therefore thread-safe to
    // call concurrently with different data ranges.
    // ------------------------------------------------------------------

    /// Apply the entire pipeline to a single pixel.
    std::array<float, 3> process(const std::array<float, 3>& pixel) const;

    /// Apply the pipeline in-place to the pixel range [startIndex, endIndex).
    /// Designed for host-side thread-pool usage: call with non-overlapping
    /// ranges from multiple threads simultaneously.
    void processPixels(float* data, size_t startIndex, size_t endIndex) const;

    /// Convenience wrapper: apply to a full image buffer.
    void processImage(float* data, size_t width, size_t height) const {
        processPixels(data, 0, width * height);
    }

private:
    std::vector<Stage> stages_;
};

} // namespace cubelut
