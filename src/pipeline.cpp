#include "cubelut/pipeline.hpp"

namespace cubelut {

void Pipeline::addStage(Lut lut, Interpolation interp) {
    stages_.push_back(Stage{std::move(lut), interp});
}

std::array<float, 3> Pipeline::process(const std::array<float, 3>& pixel) const {
    auto result = pixel;
    for (const auto& s : stages_) {
        result = Processor::process(s.lut, result, s.interp);
    }
    return result;
}

void Pipeline::processPixels(float* data, size_t startIndex, size_t endIndex) const {
    if (!data || startIndex >= endIndex) return;
    for (const auto& s : stages_) {
        Processor::processPixels(s.lut, data, startIndex, endIndex, s.interp);
    }
}

void Pipeline::processImageParallel(float* data, size_t width, size_t height,
                                    unsigned numThreads) const {
    if (!data) return;
    // Stages are applied sequentially (inter-stage data dependency cannot be parallelized).
    // Pixels WITHIN each stage are processed in parallel via platform-adaptive dispatch.
    for (const auto& s : stages_) {
        Processor::processImageParallel(s.lut, data, width, height, s.interp, numThreads);
    }
}

} // namespace cubelut
