#include "cubelut/baker.hpp"
#include <cassert>

namespace cubelut {

// ---------------------------------------------------------------------------
// Identity generators
// ---------------------------------------------------------------------------

LutData3D Baker::makeIdentity3D(int size) {
    if (size < 2) size = 2;

    LutData3D grid;
    grid.size = size;
    grid.data.resize(static_cast<size_t>(size) * size * size * 3);

    // Generate Red-fastest (Blue-major) lattice — the canonical .cube order.
    // Index = b*size² + g*size + r  → values = (r, g, b) / (size-1)
    const float inv = 1.0f / static_cast<float>(size - 1);
    size_t idx = 0;
    for (int b = 0; b < size; ++b)
    for (int g = 0; g < size; ++g)
    for (int r = 0; r < size; ++r) {
        grid.data[idx++] = r * inv;  // R
        grid.data[idx++] = g * inv;  // G
        grid.data[idx++] = b * inv;  // B
    }

    return grid;
}

LutData1D Baker::makeIdentity1D(int size) {
    if (size < 2) size = 2;

    LutData1D lut;
    lut.size = size;
    lut.data.resize(static_cast<size_t>(size) * 3);

    const float inv = 1.0f / static_cast<float>(size - 1);
    for (int i = 0; i < size; ++i) {
        const float v    = i * inv;
        lut.data[i*3+0]  = v;
        lut.data[i*3+1]  = v;
        lut.data[i*3+2]  = v;
    }

    return lut;
}

// ---------------------------------------------------------------------------
// Bake from Pipeline
// ---------------------------------------------------------------------------

LutData3D Baker::bake3D(const Pipeline& pipeline, int size) {
    if (size < 2) size = 2;

    // Start with the identity grid, then apply the pipeline in-place.
    // Pipeline::processImage() uses the existing SIMD path automatically.
    auto grid = makeIdentity3D(size);
    const size_t total_pixels = static_cast<size_t>(size) * size * size;
    pipeline.processImage(grid.data.data(), total_pixels, 1);
    return grid;
}

} // namespace cubelut
