#pragma once

#include <array>
#include <type_traits>
#include "lut.hpp"
#include "pipeline.hpp"

namespace cubelut {

// ---------------------------------------------------------------------------
// Baker – generates LutData from a Pipeline, a callable, or from scratch.
//
// Symmetric roles in the cubelut data-flow:
//   Parser reads  .cube → Lut              (input)
//   Baker  builds       → LutData3D        (generate/bake)
//   Writer writes Lut → .cube              (output)
//
// Usage:
//   // Collapse two LUTs into one 33³ grid via Pipeline:
//   Pipeline pipe;
//   pipe.addStage(*Parser::fromFile("log2lin.cube"));
//   pipe.addStage(*Parser::fromFile("film_grade.cube"));
//
//   Lut combined;
//   combined.title  = "Baked";
//   combined.grid3D = Baker::bake3D(pipe, 33);
//   Writer::toFile(combined, "combined.cube");
//
//   // Bake a lambda (e.g. colour inversion):
//   auto grid = Baker::bake3D([](std::array<float,3> p) {
//       return std::array<float,3>{1-p[0], 1-p[1], 1-p[2]};
//   }, 17);
// ---------------------------------------------------------------------------

class Baker {
public:
    // ------------------------------------------------------------------
    // Identity generators
    // ------------------------------------------------------------------

    /// Return a size×size×size identity LUT grid (Blue-major, Red-fastest).
    /// Minimum valid size is 2; values below 2 are clamped to 2.
    static LutData3D makeIdentity3D(int size = 33);

    /// Return a linear-ramp 1D identity LUT of length `size`.
    static LutData1D makeIdentity1D(int size = 4096);

    // ------------------------------------------------------------------
    // Bake from Pipeline  (SIMD-accelerated via processImage)
    // ------------------------------------------------------------------

    /// Sample every lattice point of a size³ identity grid through `pipeline`.
    /// Uses the pipeline's existing SIMD path — baking 33³ ≈ 0.1 ms.
    static LutData3D bake3D(const Pipeline& pipeline, int size = 33);

    // ------------------------------------------------------------------
    // Bake from any callable  (scalar – suitable for lambdas / functors)
    // ------------------------------------------------------------------

    /// Sample every lattice point through callable `fn`.
    /// Signature required: std::array<float,3> fn(std::array<float,3>)
    ///
    /// SFINAE guard excludes Pipeline so this never shadows the Pipeline overload.
    template<typename Fn,
             typename = std::enable_if_t<!std::is_same_v<std::decay_t<Fn>, Pipeline>>>
    static LutData3D bake3D(Fn&& fn, int size = 33) {
        if (size < 2) size = 2;
        auto grid = makeIdentity3D(size);
        const size_t total = static_cast<size_t>(size) * size * size;
        for (size_t i = 0; i < total; ++i) {
            std::array<float, 3> px = {
                grid.data[i * 3 + 0],
                grid.data[i * 3 + 1],
                grid.data[i * 3 + 2]
            };
            auto out = fn(px);
            grid.data[i * 3 + 0] = out[0];
            grid.data[i * 3 + 1] = out[1];
            grid.data[i * 3 + 2] = out[2];
        }
        return grid;
    }
};

} // namespace cubelut
