#include "cubelut/processor.hpp"
#include <algorithm>
#include <cmath>

// ====== Google Highway SIMD Setup ======
#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "processor.cpp"
#include <hwy/foreach_target.h>
#include <hwy/highway.h>

HWY_BEFORE_NAMESPACE();
namespace cubelut {
namespace HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

// -------------------------------------------------------------
// 1. Trilinear SIMD Impl
// -------------------------------------------------------------
size_t ProcessPixels3DSIMD_Trilinear_Bulk(const LutData3D& lut, float* data, size_t startPixel, size_t endPixel) {
    const hn::ScalableTag<float> df;
    const hn::ScalableTag<int32_t> di;
    const size_t N = hn::Lanes(df);

    float scale_r = (lut.size - 1) / (lut.domain.max[0] - lut.domain.min[0]);
    float scale_g = (lut.size - 1) / (lut.domain.max[1] - lut.domain.min[1]);
    float scale_b = (lut.size - 1) / (lut.domain.max[2] - lut.domain.min[2]);
    auto v_min_r = hn::Set(df, lut.domain.min[0]);
    auto v_min_g = hn::Set(df, lut.domain.min[1]);
    auto v_min_b = hn::Set(df, lut.domain.min[2]);

    auto v_scale_r = hn::Set(df, scale_r);
    auto v_scale_g = hn::Set(df, scale_g);
    auto v_scale_b = hn::Set(df, scale_b);
    auto v_zero = hn::Zero(df);
    auto v_size_m1 = hn::Set(df, static_cast<float>(lut.size - 1));
    auto v_max_idx = hn::Set(di, lut.size - 2);

    int lut_size = lut.size, lut_size2 = lut.size * lut.size;
    const float* lut_data = lut.data.data();

    size_t i = startPixel;
    for (; i + N <= endPixel; i += N) {
        hn::Vec<decltype(df)> r, g, b;
        hn::LoadInterleaved3(df, data + i * 3, r, g, b);

        r = hn::Clamp(hn::Mul(hn::Sub(r, v_min_r), v_scale_r), v_zero, v_size_m1);
        g = hn::Clamp(hn::Mul(hn::Sub(g, v_min_g), v_scale_g), v_zero, v_size_m1);
        b = hn::Clamp(hn::Mul(hn::Sub(b, v_min_b), v_scale_b), v_zero, v_size_m1);

        auto idx_r = hn::Min(hn::ConvertTo(di, hn::Floor(r)), v_max_idx);
        auto idx_g = hn::Min(hn::ConvertTo(di, hn::Floor(g)), v_max_idx);
        auto idx_b = hn::Min(hn::ConvertTo(di, hn::Floor(b)), v_max_idx);
        auto frac_r = hn::Sub(r, hn::ConvertTo(df, idx_r));
        auto frac_g = hn::Sub(g, hn::ConvertTo(df, idx_g));
        auto frac_b = hn::Sub(b, hn::ConvertTo(df, idx_b));

        auto offset000 = hn::Mul(hn::Add(hn::Add(hn::Mul(idx_b, hn::Set(di, lut_size2)), hn::Mul(idx_g, hn::Set(di, lut_size))), idx_r), hn::Set(di, 3));
        auto offset100 = hn::Add(offset000, hn::Set(di, 3));
        auto offset010 = hn::Add(offset000, hn::Set(di, 3 * lut_size));
        auto offset110 = hn::Add(offset010, hn::Set(di, 3));
        auto offset001 = hn::Add(offset000, hn::Set(di, 3 * lut_size2));
        auto offset101 = hn::Add(offset001, hn::Set(di, 3));
        auto offset011 = hn::Add(offset001, hn::Set(di, 3 * lut_size));
        auto offset111 = hn::Add(offset011, hn::Set(di, 3));

        auto gather_rgb = [&](auto offset, auto& out_r, auto& out_g, auto& out_b) {
            out_r = hn::GatherIndex(df, lut_data + 0, offset);
            out_g = hn::GatherIndex(df, lut_data + 1, offset);
            out_b = hn::GatherIndex(df, lut_data + 2, offset);
        };

        hn::Vec<decltype(df)> r000, g000, b000; gather_rgb(offset000, r000, g000, b000);
        hn::Vec<decltype(df)> r100, g100, b100; gather_rgb(offset100, r100, g100, b100);
        hn::Vec<decltype(df)> r010, g010, b010; gather_rgb(offset010, r010, g010, b010);
        hn::Vec<decltype(df)> r110, g110, b110; gather_rgb(offset110, r110, g110, b110);
        hn::Vec<decltype(df)> r001, g001, b001; gather_rgb(offset001, r001, g001, b001);
        hn::Vec<decltype(df)> r101, g101, b101; gather_rgb(offset101, r101, g101, b101);
        hn::Vec<decltype(df)> r011, g011, b011; gather_rgb(offset011, r011, g011, b011);
        hn::Vec<decltype(df)> r111, g111, b111; gather_rgb(offset111, r111, g111, b111);

        auto lerp = [](auto a, auto b, auto t) { return hn::MulAdd(t, hn::Sub(b, a), a); };
        auto interp_channel = [&](auto c000, auto c100, auto c010, auto c110, auto c001, auto c101, auto c011, auto c111) {
            return lerp(lerp(lerp(c000, c100, frac_r), lerp(c010, c110, frac_r), frac_g),
                        lerp(lerp(c001, c101, frac_r), lerp(c011, c111, frac_r), frac_g), frac_b);
        };

        r = interp_channel(r000, r100, r010, r110, r001, r101, r011, r111);
        g = interp_channel(g000, g100, g010, g110, g001, g101, g011, g111);
        b = interp_channel(b000, b100, b010, b110, b001, b101, b011, b111);
        hn::StoreInterleaved3(r, g, b, df, data + i * 3);
    }
    return i;
}

// -------------------------------------------------------------
// 2. Tetrahedral SIMD Impl (Branchless)
// -------------------------------------------------------------
size_t ProcessPixels3DSIMD_Tetrahedral_Bulk(const LutData3D& lut, float* data, size_t startPixel, size_t endPixel) {
    const hn::ScalableTag<float> df;
    const hn::ScalableTag<int32_t> di;
    const size_t N = hn::Lanes(df);

    float scale_r = (lut.size - 1) / (lut.domain.max[0] - lut.domain.min[0]);
    float scale_g = (lut.size - 1) / (lut.domain.max[1] - lut.domain.min[1]);
    float scale_b = (lut.size - 1) / (lut.domain.max[2] - lut.domain.min[2]);
    auto v_min_r = hn::Set(df, lut.domain.min[0]);
    auto v_min_g = hn::Set(df, lut.domain.min[1]);
    auto v_min_b = hn::Set(df, lut.domain.min[2]);
    auto v_scale_r = hn::Set(df, scale_r);
    auto v_scale_g = hn::Set(df, scale_g);
    auto v_scale_b = hn::Set(df, scale_b);
    auto v_zero = hn::Zero(df);
    auto v_size_m1 = hn::Set(df, static_cast<float>(lut.size - 1));
    auto v_max_idx = hn::Set(di, lut.size - 2);

    int lut_size = lut.size, lut_size2 = lut.size * lut.size;
    const float* lut_data = lut.data.data();

    auto step_r = hn::Set(di, 3);
    auto step_g = hn::Set(di, 3 * lut_size);
    auto step_b = hn::Set(di, 3 * lut_size2);

    size_t i = startPixel;
    for (; i + N <= endPixel; i += N) {
        hn::Vec<decltype(df)> r, g, b;
        hn::LoadInterleaved3(df, data + i * 3, r, g, b);

        r = hn::Clamp(hn::Mul(hn::Sub(r, v_min_r), v_scale_r), v_zero, v_size_m1);
        g = hn::Clamp(hn::Mul(hn::Sub(g, v_min_g), v_scale_g), v_zero, v_size_m1);
        b = hn::Clamp(hn::Mul(hn::Sub(b, v_min_b), v_scale_b), v_zero, v_size_m1);

        auto idx_r = hn::Min(hn::ConvertTo(di, hn::Floor(r)), v_max_idx);
        auto idx_g = hn::Min(hn::ConvertTo(di, hn::Floor(g)), v_max_idx);
        auto idx_b = hn::Min(hn::ConvertTo(di, hn::Floor(b)), v_max_idx);

        auto d_r = hn::Sub(r, hn::ConvertTo(df, idx_r));
        auto d_g = hn::Sub(g, hn::ConvertTo(df, idx_g));
        auto d_b = hn::Sub(b, hn::ConvertTo(df, idx_b));

        // Sort fractions: x0 >= x1 >= x2
        auto rg_min = hn::Min(d_r, d_g);
        auto rg_max = hn::Max(d_r, d_g);
        auto x2 = hn::Min(rg_min, d_b);
        auto mid = hn::Max(rg_min, d_b);
        auto x0 = hn::Max(rg_max, d_b);
        auto x1 = hn::Min(rg_max, mid);

        // Boolean masks to find ca and cb path nodes
        auto gt_r_f = hn::Gt(d_r, d_g);
        auto gt_g_f = hn::Gt(d_g, d_b);
        auto gt_b_f = hn::Gt(d_b, d_r);
        auto gt_r = hn::RebindMask(di, gt_r_f);
        auto gt_g = hn::RebindMask(di, gt_g_f);
        auto gt_b = hn::RebindMask(di, gt_b_f);

        auto max_r = hn::AndNot(gt_b, gt_r); // !b>r && r>g
        auto max_g = hn::AndNot(gt_r, gt_g);
        auto max_b = hn::AndNot(gt_g, gt_b);

        auto min_r = hn::AndNot(gt_r, gt_b); // !r>g && b>r
        auto min_g = hn::AndNot(gt_g, gt_r);
        auto min_b = hn::AndNot(gt_b, gt_g);

        // Memory Offsets for prev (x) and next (x+1) boundaries
        auto off_prev_r = hn::Mul(idx_r, step_r);
        auto off_next_r = hn::Add(off_prev_r, step_r);
        auto off_prev_g = hn::Mul(idx_g, step_g);
        auto off_next_g = hn::Add(off_prev_g, step_g);
        auto off_prev_b = hn::Mul(idx_b, step_b);
        auto off_next_b = hn::Add(off_prev_b, step_b);

        // Base C000 and C111 1D index
        auto off_c000 = hn::Add(hn::Add(off_prev_r, off_prev_g), off_prev_b);
        auto off_c111 = hn::Add(hn::Add(off_next_r, off_next_g), off_next_b);

        // Node CA: Takes 'next' only for the maximum dimension, else 'prev'
        auto off_ca_r = hn::IfThenElse(max_r, off_next_r, off_prev_r);
        auto off_ca_g = hn::IfThenElse(max_g, off_next_g, off_prev_g);
        auto off_ca_b = hn::IfThenElse(max_b, off_next_b, off_prev_b);
        auto off_ca = hn::Add(hn::Add(off_ca_r, off_ca_g), off_ca_b);

        // Node CB: Takes 'prev' only for the minimum dimension, else 'next'
        auto off_cb_r = hn::IfThenElse(min_r, off_prev_r, off_next_r);
        auto off_cb_g = hn::IfThenElse(min_g, off_prev_g, off_next_g);
        auto off_cb_b = hn::IfThenElse(min_b, off_prev_b, off_next_b);
        auto off_cb = hn::Add(hn::Add(off_cb_r, off_cb_g), off_cb_b);

        // Parallel Gather 4 Vertices ONLY
        auto gather_rgb = [&](auto offset, auto& out_r, auto& out_g, auto& out_b) {
            out_r = hn::GatherIndex(df, lut_data + 0, offset);
            out_g = hn::GatherIndex(df, lut_data + 1, offset);
            out_b = hn::GatherIndex(df, lut_data + 2, offset);
        };
        hn::Vec<decltype(df)> r000, g000, b000; gather_rgb(off_c000, r000, g000, b000);
        hn::Vec<decltype(df)> ra, ga, ba;       gather_rgb(off_ca, ra, ga, ba);
        hn::Vec<decltype(df)> rb, gb, bb;       gather_rgb(off_cb, rb, gb, bb);
        hn::Vec<decltype(df)> r111, g111, b111; gather_rgb(off_c111, r111, g111, b111);

        // Blending equation: c000 + x0*(ca-c000) + x1*(cb-ca) + x2*(c111-cb)
        auto blend_channel = [&](auto c000, auto ca, auto cb, auto c111) {
            auto res = hn::MulAdd(x0, hn::Sub(ca, c000), c000);
            res = hn::MulAdd(x1, hn::Sub(cb, ca), res);
            return hn::MulAdd(x2, hn::Sub(c111, cb), res);
        };

        r = blend_channel(r000, ra, rb, r111);
        g = blend_channel(g000, ga, gb, g111);
        b = blend_channel(b000, ba, bb, b111);

        hn::StoreInterleaved3(r, g, b, df, data + i * 3);
    }
    return i;
}


// -------------------------------------------------------------
// 3. 1D Shaper SIMD Impl
// -------------------------------------------------------------
size_t ProcessPixels1DSIMD_Bulk(const LutData1D& lut, float* data, size_t startPixel, size_t endPixel) {
    const hn::ScalableTag<float> df;
    const hn::ScalableTag<int32_t> di;
    const size_t N = hn::Lanes(df);

    // Degenerate 1-entry LUT: fall back to scalar path.
    if (lut.size < 2) return startPixel;

    float size_m1 = static_cast<float>(lut.size - 1);
    auto v_min_r   = hn::Set(df, lut.domain.min[0]);
    auto v_min_g   = hn::Set(df, lut.domain.min[1]);
    auto v_min_b   = hn::Set(df, lut.domain.min[2]);
    auto v_scale_r = hn::Set(df, size_m1 / (lut.domain.max[0] - lut.domain.min[0]));
    auto v_scale_g = hn::Set(df, size_m1 / (lut.domain.max[1] - lut.domain.min[1]));
    auto v_scale_b = hn::Set(df, size_m1 / (lut.domain.max[2] - lut.domain.min[2]));
    auto v_zero    = hn::Zero(df);
    auto v_size_m1 = hn::Set(df, size_m1);
    auto v_max_idx = hn::Set(di, lut.size - 2);  // floor index is clamped to [0, size-2]
    auto v_step3   = hn::Set(di, 3);             // stride between entries: R0G0B0 R1G1B1 ...
    const float* lut_data = lut.data.data();

    size_t i = startPixel;
    for (; i + N <= endPixel; i += N) {
        hn::Vec<decltype(df)> r, g, b;
        hn::LoadInterleaved3(df, data + i * 3, r, g, b);

        // Normalize to LUT coordinate space
        r = hn::Clamp(hn::Mul(hn::Sub(r, v_min_r), v_scale_r), v_zero, v_size_m1);
        g = hn::Clamp(hn::Mul(hn::Sub(g, v_min_g), v_scale_g), v_zero, v_size_m1);
        b = hn::Clamp(hn::Mul(hn::Sub(b, v_min_b), v_scale_b), v_zero, v_size_m1);

        // Floor index, clamped to valid interpolation range [0, size-2]
        auto idx_r = hn::Min(hn::ConvertTo(di, hn::Floor(r)), v_max_idx);
        auto idx_g = hn::Min(hn::ConvertTo(di, hn::Floor(g)), v_max_idx);
        auto idx_b = hn::Min(hn::ConvertTo(di, hn::Floor(b)), v_max_idx);

        // Fractional parts
        auto frac_r = hn::Sub(r, hn::ConvertTo(df, idx_r));
        auto frac_g = hn::Sub(g, hn::ConvertTo(df, idx_g));
        auto frac_b = hn::Sub(b, hn::ConvertTo(df, idx_b));

        // LUT data layout: [R0, G0, B0, R1, G1, B1, ...]
        // R channel uses lut_data[idx*3 + 0]; G uses +1; B uses +2.
        // GatherIndex(df, base, offsets) loads base[offsets[lane]] per lane.
        auto off_r0 = hn::Mul(idx_r, v_step3);
        auto off_r1 = hn::Add(off_r0, v_step3);
        auto off_g0 = hn::Mul(idx_g, v_step3);
        auto off_g1 = hn::Add(off_g0, v_step3);
        auto off_b0 = hn::Mul(idx_b, v_step3);
        auto off_b1 = hn::Add(off_b0, v_step3);

        auto r0 = hn::GatherIndex(df, lut_data + 0, off_r0);
        auto r1 = hn::GatherIndex(df, lut_data + 0, off_r1);
        auto g0 = hn::GatherIndex(df, lut_data + 1, off_g0);
        auto g1 = hn::GatherIndex(df, lut_data + 1, off_g1);
        auto b0 = hn::GatherIndex(df, lut_data + 2, off_b0);
        auto b1 = hn::GatherIndex(df, lut_data + 2, off_b1);

        // Linear interpolation: result = v0 + frac * (v1 - v0)
        r = hn::MulAdd(frac_r, hn::Sub(r1, r0), r0);
        g = hn::MulAdd(frac_g, hn::Sub(g1, g0), g0);
        b = hn::MulAdd(frac_b, hn::Sub(b1, b0), b0);

        hn::StoreInterleaved3(r, g, b, df, data + i * 3);
    }
    return i;
}


// -------------------------------------------------------------
// 4. RGB float32 → RGBA float32 packing
// -------------------------------------------------------------
size_t PackRGBToRGBA32_Bulk(const float* HWY_RESTRICT rgb_in,
                             float*       HWY_RESTRICT rgba_out,
                             size_t numPixels) {
    const hn::ScalableTag<float> df;
    const size_t N = hn::Lanes(df);
    const auto v_alpha = hn::Set(df, 1.0f);

    size_t i = 0;
    for (; i + N <= numPixels; i += N) {
        hn::Vec<decltype(df)> r, g, b;
        hn::LoadInterleaved3(df, rgb_in + i * 3, r, g, b);
        hn::StoreInterleaved4(r, g, b, v_alpha, df, rgba_out + i * 4);
    }
    return i;
}

// -------------------------------------------------------------
// 5. RGB float32 → RGBA float16 packing
//    Strategy:
//    - Load N float32 pixels per lane group
//    - DemoteTo(df16, ch32) converts each float32 channel to float16
//      (same lane count N, output vector is N*2 bytes instead of N*4)
//    - BitCast float16 -> uint16 for type-safe storage
//    - StoreInterleaved4 writes [R16, G16, B16, A16] interleaved
// -------------------------------------------------------------
size_t PackRGBToRGBA16_Bulk(const float*   HWY_RESTRICT rgb_in,
                             uint16_t*      HWY_RESTRICT rgba16_out,
                             size_t numPixels) {
    const hn::ScalableTag<float> df32;
    // float16 tag: same lane count N, each lane 2 bytes
    const hn::Rebind<hwy::float16_t, decltype(df32)> df16;
    // uint16 tag: same shape as df16 for BitCast + StoreInterleaved4
    const hn::RebindToUnsigned<decltype(df16)> du16;
    const size_t N = hn::Lanes(df32);

    // IEEE 754 half-precision 1.0f = 0x3C00
    const auto v_alpha = hn::Set(du16, uint16_t{0x3C00});

    size_t i = 0;
    for (; i + N <= numPixels; i += N) {
        hn::Vec<decltype(df32)> r32, g32, b32;
        hn::LoadInterleaved3(df32, rgb_in + i * 3, r32, g32, b32);

        // DemoteTo: float32 → float16 (N lanes, vector shrinks by half)
        // BitCast:  float16 → uint16  (same bits, type-safe for storage)
        auto r_u16 = hn::BitCast(du16, hn::DemoteTo(df16, r32));
        auto g_u16 = hn::BitCast(du16, hn::DemoteTo(df16, g32));
        auto b_u16 = hn::BitCast(du16, hn::DemoteTo(df16, b32));

        hn::StoreInterleaved4(r_u16, g_u16, b_u16, v_alpha, du16, rgba16_out + i * 4);
    }
    return i;
}

} // HWY_NAMESPACE
} // namespace cubelut
HWY_AFTER_NAMESPACE();

// ====== ONCE BLOCK ======
#if HWY_ONCE
namespace cubelut {

HWY_EXPORT(ProcessPixels3DSIMD_Trilinear_Bulk);
HWY_EXPORT(ProcessPixels3DSIMD_Tetrahedral_Bulk);
HWY_EXPORT(ProcessPixels1DSIMD_Bulk);
HWY_EXPORT(PackRGBToRGBA32_Bulk);
HWY_EXPORT(PackRGBToRGBA16_Bulk);

static float clamp(float v, float min, float max) { return std::max(min, std::min(max, v)); }
static float lerp(float a, float b, float t) { return a + t * (b - a); }

std::array<float, 3> Processor::process1D(const LutData1D& lut, const std::array<float, 3>& pixel) {
    std::array<float, 3> result;
    float size_m1 = static_cast<float>(lut.size - 1);
    for (int i = 0; i < 3; ++i) {
        float normalized = (pixel[i] - lut.domain.min[i]) / (lut.domain.max[i] - lut.domain.min[i]);
        float pos = clamp(normalized, 0.0f, 1.0f) * size_m1;
        int idx = static_cast<int>(std::floor(pos));
        float frac = pos - static_cast<float>(idx);
        if (idx >= lut.size - 1) {
            result[i] = lut.data[ (lut.size - 1) * 3 + i ];
        } else {
            float v1 = lut.data[ idx * 3 + i ];
            float v2 = lut.data[ (idx + 1) * 3 + i ];
            result[i] = lerp(v1, v2, frac);
        }
    }
    return result;
}

// 1. Trilinear Scalar fallback
std::array<float, 3> Processor::process3DTrilinear(const LutData3D& lut, const std::array<float, 3>& pixel) {
    float size_m1 = static_cast<float>(lut.size - 1);
    std::array<float, 3> coords, frac;
    std::array<int, 3> idx;
    
    for (int i = 0; i < 3; ++i) {
        float normalized = (pixel[i] - lut.domain.min[i]) / (lut.domain.max[i] - lut.domain.min[i]);
        coords[i] = clamp(normalized, 0.0f, 1.0f) * size_m1;
        idx[i] = std::min(static_cast<int>(std::floor(coords[i])), lut.size - 2);
        frac[i] = coords[i] - static_cast<float>(idx[i]);
    }
    
    auto get_lut_val = [&](int r_idx, int g_idx, int b_idx) -> std::array<float, 3> {
        size_t index = (static_cast<size_t>(b_idx) * lut.size * lut.size + static_cast<size_t>(g_idx) * lut.size + static_cast<size_t>(r_idx)) * 3;
        return {lut.data[index], lut.data[index + 1], lut.data[index + 2]};
    };
    
    auto c000 = get_lut_val(idx[0],     idx[1],     idx[2]);
    auto c100 = get_lut_val(idx[0] + 1, idx[1],     idx[2]);
    auto c010 = get_lut_val(idx[0],     idx[1] + 1, idx[2]);
    auto c110 = get_lut_val(idx[0] + 1, idx[1] + 1, idx[2]);
    auto c001 = get_lut_val(idx[0],     idx[1],     idx[2] + 1);
    auto c101 = get_lut_val(idx[0] + 1, idx[1],     idx[2] + 1);
    auto c011 = get_lut_val(idx[0],     idx[1] + 1, idx[2] + 1);
    auto c111 = get_lut_val(idx[0] + 1, idx[1] + 1, idx[2] + 1);
    
    std::array<float, 3> result;
    for (int i = 0; i < 3; ++i) {
        float c00 = lerp(c000[i], c100[i], frac[0]);
        float c10 = lerp(c010[i], c110[i], frac[0]);
        float c01 = lerp(c001[i], c101[i], frac[0]);
        float c11 = lerp(c011[i], c111[i], frac[0]);
        float c0 = lerp(c00, c10, frac[1]);
        float c1 = lerp(c01, c11, frac[1]);
        result[i] = lerp(c0, c1, frac[2]);
    }
    return result;
}

// 2. Tetrahedral Scalar fallback
std::array<float, 3> Processor::process3DTetrahedral(const LutData3D& lut, const std::array<float, 3>& pixel) {
    float size_m1 = static_cast<float>(lut.size - 1);
    std::array<float, 3> frac;
    std::array<int, 3> idx;
    
    for (int i = 0; i < 3; ++i) {
        float normalized = (pixel[i] - lut.domain.min[i]) / (lut.domain.max[i] - lut.domain.min[i]);
        float coords = clamp(normalized, 0.0f, 1.0f) * size_m1;
        idx[i] = std::min(static_cast<int>(std::floor(coords)), lut.size - 2);
        frac[i] = coords - static_cast<float>(idx[i]);
    }

    auto get_lut_val = [&](int r_idx, int g_idx, int b_idx) -> std::array<float, 3> {
        size_t index = (static_cast<size_t>(b_idx) * lut.size * lut.size + static_cast<size_t>(g_idx) * lut.size + static_cast<size_t>(r_idx)) * 3;
        return {lut.data[index], lut.data[index + 1], lut.data[index + 2]};
    };

    float d_r = frac[0], d_g = frac[1], d_b = frac[2];
    int rx0 = idx[0], rx1 = idx[0] + 1;
    int gx0 = idx[1], gx1 = idx[1] + 1;
    int bx0 = idx[2], bx1 = idx[2] + 1;

    auto c000 = get_lut_val(rx0, gx0, bx0);
    auto c111 = get_lut_val(rx1, gx1, bx1);
    std::array<float, 3> ca, cb;

    // Geometric tetrahedral cut determination
    if (d_r > d_g) {
        if (d_g > d_b) {      // R > G > B
            ca = get_lut_val(rx1, gx0, bx0);
            cb = get_lut_val(rx1, gx1, bx0);
        } else if (d_r > d_b) { // R > B > G
            ca = get_lut_val(rx1, gx0, bx0);
            cb = get_lut_val(rx1, gx0, bx1);
        } else {              // B > R > G
            ca = get_lut_val(rx0, gx0, bx1);
            cb = get_lut_val(rx1, gx0, bx1);
        }
    } else {
        if (d_b > d_g) {      // B > G > R
            ca = get_lut_val(rx0, gx0, bx1);
            cb = get_lut_val(rx0, gx1, bx1);
        } else if (d_b > d_r) { // G > B > R
            ca = get_lut_val(rx0, gx1, bx0);
            cb = get_lut_val(rx0, gx1, bx1);
        } else {              // G > R > B
            ca = get_lut_val(rx0, gx1, bx0);
            cb = get_lut_val(rx1, gx1, bx0);
        }
    }

    float x0 = std::max({d_r, d_g, d_b});
    float x2 = std::min({d_r, d_g, d_b});
    float x1 = d_r + d_g + d_b - x0 - x2;

    std::array<float, 3> result;
    for (int i = 0; i < 3; ++i) {
        result[i] = c000[i] + x0 * (ca[i] - c000[i]) + x1 * (cb[i] - ca[i]) + x2 * (c111[i] - cb[i]);
    }
    return result;
}

std::array<float, 3> Processor::process(const Lut& lut, const std::array<float, 3>& pixel, Interpolation interp) {
    if (!lut.isValid()) return pixel;
    
    std::array<float, 3> result = pixel;

    if (lut.shaper1D.has_value()) {
        result = process1D(*lut.shaper1D, result);
    }
    
    if (lut.grid3D.has_value()) {
        if (interp == Interpolation::Tetrahedral) {
            result = process3DTetrahedral(*lut.grid3D, result);
        } else {
            result = process3DTrilinear(*lut.grid3D, result);
        }
    }
    
    return result;
}

void Processor::processPixels(const Lut& lut, float* data, size_t startIndex, size_t endIndex, Interpolation interp) {
    if (!lut.isValid() || !data || startIndex >= endIndex) return;
    
    if (lut.shaper1D.has_value()) {
        const auto& shaper = *lut.shaper1D;
        // Dispatch to the SIMD-accelerated 1D shaper bulk processor.
        size_t i = HWY_DYNAMIC_DISPATCH(ProcessPixels1DSIMD_Bulk)(shaper, data, startIndex, endIndex);
        // Scalar tail: handles remaining pixels when total count is not a multiple of SIMD lane width.
        for (; i < endIndex; ++i) {
            std::array<float, 3> pixel = {data[i * 3], data[i * 3 + 1], data[i * 3 + 2]};
            auto result = process1D(shaper, pixel);
            data[i * 3]     = result[0];
            data[i * 3 + 1] = result[1];
            data[i * 3 + 2] = result[2];
        }
    }
    
    if (lut.grid3D.has_value()) {
        size_t i = startIndex;
        const auto& grid = *lut.grid3D;
        if (interp == Interpolation::Tetrahedral) {
            i = HWY_DYNAMIC_DISPATCH(ProcessPixels3DSIMD_Tetrahedral_Bulk)(grid, data, startIndex, endIndex);
        } else {
            i = HWY_DYNAMIC_DISPATCH(ProcessPixels3DSIMD_Trilinear_Bulk)(grid, data, startIndex, endIndex);
        }
        
        // Tail pixels (if endIndex is not aligned with SIMD lanes)
        for (; i < endIndex; ++i) {
            std::array<float, 3> pixel = {data[i * 3], data[i * 3 + 1], data[i * 3 + 2]};
            auto result = (interp == Interpolation::Tetrahedral) ? process3DTetrahedral(grid, pixel) : process3DTrilinear(grid, pixel);
            data[i * 3] = result[0];
            data[i * 3 + 1] = result[1];
            data[i * 3 + 2] = result[2];
        }
    }
}

void Processor::convertRGBToRGBA32(const float* rgb, float* rgba, size_t numPixels) {
    if (!rgb || !rgba || numPixels == 0) return;
    size_t i = HWY_DYNAMIC_DISPATCH(PackRGBToRGBA32_Bulk)(rgb, rgba, numPixels);
    // Scalar tail for remaining pixels not covered by a full SIMD vector
    for (; i < numPixels; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0];
        rgba[i * 4 + 1] = rgb[i * 3 + 1];
        rgba[i * 4 + 2] = rgb[i * 3 + 2];
        rgba[i * 4 + 3] = 1.0f;
    }
}

void Processor::convertRGBToRGBA16(const float* rgb, uint16_t* rgba16, size_t numPixels) {
    if (!rgb || !rgba16 || numPixels == 0) return;
    size_t i = HWY_DYNAMIC_DISPATCH(PackRGBToRGBA16_Bulk)(rgb, rgba16, numPixels);
    // Scalar tail: use the same rounding-correct float_to_half as before
    constexpr uint16_t kAlphaFP16 = 0x3C00;  // 1.0f in IEEE 754 half
    auto scalar_f32_to_f16 = [](float v) -> uint16_t {
        // Ryg's round-to-nearest-even float-to-half (public domain)
        union FP32 { uint32_t u; float f; };
        FP32 f; f.f = v;
        FP32 f32inf  = {255u << 23};
        FP32 f16max  = {(127u + 16u) << 23};
        FP32 magic   = {((127u - 15u) + (23u - 10u) + 1u) << 23};
        uint32_t sign = f.u & 0x80000000u;
        f.u ^= sign;
        uint16_t o;
        if (f.u >= f16max.u) {
            o = (f.u > f32inf.u) ? uint16_t{0x7e00} : uint16_t{0x7c00};
        } else if (f.u < (113u << 23)) {
            f.f += magic.f;
            o = static_cast<uint16_t>(f.u - magic.u);
        } else {
            uint32_t mant_odd = (f.u >> 13) & 1u;
            f.u += (static_cast<uint32_t>(15 - 127) << 23) + 0xfffu;
            f.u += mant_odd;
            o = static_cast<uint16_t>(f.u >> 13);
        }
        return o | static_cast<uint16_t>(sign >> 16);
    };
    for (; i < numPixels; ++i) {
        rgba16[i * 4 + 0] = scalar_f32_to_f16(rgb[i * 3 + 0]);
        rgba16[i * 4 + 1] = scalar_f32_to_f16(rgb[i * 3 + 1]);
        rgba16[i * 4 + 2] = scalar_f32_to_f16(rgb[i * 3 + 2]);
        rgba16[i * 4 + 3] = kAlphaFP16;
    }
}

} // namespace cubelut
#endif
