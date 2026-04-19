# cubelut

A C++17 library for processing `.cube` LUT files (1D and 3D) and applying them to images.

## Features

- Parse Adobe `.cube` files (1D and 3D).
- Trilinear interpolation for 3D LUTs.
- Linear interpolation for 1D LUTs.
- Domain mapping support (`DOMAIN_MIN`, `DOMAIN_MAX`).
- In-place image processing.

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Usage

### Parsing a LUT

```cpp
#include "cubelut/cubelut.hpp"

auto lutOpt = cubelut::Parser::fromFile("filter.cube");
if (lutOpt) {
    const auto& lut = *lutOpt;
    // Use the lut
}
```

### Applying a LUT to a pixel

```cpp
std::array<float, 3> pixel = {0.5f, 0.5f, 0.5f};
auto result = cubelut::Processor::process(lut, pixel);
```

### Applying a LUT to an image

```cpp
// Assuming float* imageData points to RGB data
cubelut::Processor::processImage(lut, imageData, width, height);
```

## Future GPU Integration Guide (Metal / Vulkan / OpenGL)

While `cubelut` is a pure CPU-bound C++ engine, its C-API (`cubelut_c.h`) is specifically designed to act as a high-performance data provider for modern GPU rendering pipelines.

When integrating `cubelut`'s extracted LUT data (`cubelut_create_rgba16_buffer_for_grid3d`) into a GPU environment, you face a critical precision trade-off regarding the 3D texture sampler:

1. **Hardware Trilinear Interpolation (Maximum Performance):**
   - **How:** Upload the RGBA16 buffer to a 3D Texture and configure the GPU sampler to use linear filtering (e.g., `MTLSamplerMinMagFilterLinear` in Metal or `GL_LINEAR` in OpenGL).
   - **Pros:** Blazing fast. The GPU's Texture Mapping Unit (TMU) performs the 8-point trilinear interpolation in hardware with zero ALU overhead.
   - **Cons:** Many mobile GPUs (and some desktop GPUs) internally quantize the interpolation weights to 8-bit (1/256 precision) to save power. When using smaller LUT grids (e.g., 17x17x17), this 8-bit quantization introduces visible color banding and precision artifacts.

2. **Software Tetrahedral Interpolation (Cinematic Precision):**
   - **How:** Configure the GPU sampler to use nearest-neighbor filtering (e.g., `MTLSamplerMinMagFilterNearest` in Metal or `GL_NEAREST`). In your fragment/compute shader, manually fetch the 4 required vertices and compute the tetrahedral interpolation using ALU instructions (mirroring the logic in `cubelut::Processor`).
   - **Pros:** Absolute mathematical precision. This approach perfectly matches `cubelut`'s CPU output, bypassing the hardware's 8-bit weight quantization limits entirely. This is the industry-standard approach used by professional color management systems like OpenColorIO (OCIO).
   - **Cons:** Higher ALU instruction count and register pressure in the shader.

For professional color grading applications, **Software Tetrahedral Interpolation** is highly recommended.
