# cubelut

A C++17 library for processing Adobe `.cube` LUT files (1D shaper + 3D) and applying them to images. Used by **Espresso** as a CPU parse / bake engine that feeds Metal 3D textures.

## Features

- Parse `.cube` files (1D and 3D, including combined shaper + grid).
- Domain mapping (`DOMAIN_MIN` / `DOMAIN_MAX`, `INPUT_RANGE`).
- Trilinear and **tetrahedral** (Sakamoto) 3D interpolation.
- Highway SIMD for process + RGB→RGBA16 packing.
- Parallel image APIs (GCD on Apple).
- Structured parse errors (`ParseResult` / C last-error).
- **GPU export**: bake full pipeline onto a unit-cube lattice for Metal/Vulkan sampling.

## Building (local)

Dependencies (**Highway** 1.2.0, **fast_float** v6.1.1) live as git submodules under
`third_party/`. After clone:

```bash
git submodule update --init --recursive
mkdir build && cd build
cmake ..
cmake --build .
```

CMake uses the bundled trees when present (no network). If a submodule is missing,
it falls back to `FetchContent` (requires network once).

## Espresso XCFramework

From the Espresso repo root (device + simulator arm64 static libs + headers):

```bash
# Incremental (default): reuses cmake build dirs; only recompiles changed .cpp
./scripts/build_cubelut_xcframework.sh

# Full wipe + reconfigure (first machine / toolchain change)
./scripts/build_cubelut_xcframework.sh --clean
```

Output: `Espresso/cubelut.xcframework` (linked with `hwy.xcframework`).

The script sets `-DCUBELUT_BUILD_TOOLS=OFF` so examples, unit tests, and **Google Benchmark**
are not configured. Highway / fast_float come from `third_party/` submodules (no git clone
during cmake when those dirs exist). Local full dev builds keep tools on by default:

```bash
cmake -S cubelut -B cubelut/build && cmake --build cubelut/build
```

After changing C API / processor code, rebuild the XCFramework so Swift and the binary stay in lockstep.

## C API (Swift / ObjC)

Header: `include/cubelut/cubelut_c.h`

| API | Role |
|---|---|
| `cubelut_load_from_file` / `_string` | Parse; on failure set thread-local error |
| `cubelut_get_last_error` / `_message` | Structured diagnostics after failed load |
| `cubelut_create_rgba16_buffer_for_grid3d` | Raw 3D lattice only (no shaper/domain) |
| `cubelut_needs_gpu_pipeline_bake` | True if raw lattice ≠ CPU process on [0,1]³ |
| `cubelut_create_rgba16_for_gpu_export` | **Preferred for GPU**: bake if needed, pack FP16 |
| `cubelut_process_pixels` / parallel / u8 | CPU apply |

### GPU integration contract

Metal/Vulkan shaders that sample a 3D texture with coordinates in **linear [0,1]³** (as Espresso’s tetrahedral fragment/compute does) must upload a lattice that already includes shaper + domain.

1. Call **`cubelut_create_rgba16_for_gpu_export`** (not the raw grid helper alone).
2. Upload as `RGBA16Float` 3D texture, size = `out_grid_size`.
3. Sample with **nearest** filtering and software tetrahedral interpolation (matches `Processor` / OpenColorIO practice). Hardware trilinear is faster but mobile GPUs may quantize weights to ~8-bit and band small cubes.

**Do not** upload raw `create_rgba16_buffer_for_grid3d` when `cubelut_needs_gpu_pipeline_bake()` is true — preview/capture will disagree with CPU reference.

## C++ usage

```cpp
#include "cubelut/cubelut.hpp"

auto result = cubelut::Parser::parseFile("filter.cube");
if (!result.ok()) {
    // result.error, result.errorMessage
}
const auto& lut = *result.lut;
std::array<float, 3> pixel = {0.5f, 0.5f, 0.5f};
auto out = cubelut::Processor::process(lut, pixel); // tetrahedral default
```

## Precision note

For professional grading, prefer **software tetrahedral** on the GPU over hardware trilinear when using small grids (17³ / 33³).
