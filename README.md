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
