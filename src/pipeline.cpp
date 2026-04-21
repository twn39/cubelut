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

} // namespace cubelut
