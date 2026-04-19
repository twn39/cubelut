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
size_t ProcessImage3DSIMD_Trilinear_Bulk(const LutData3D& lut, float* data, size_t numPixels) {
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

    size_t i = 0;
    for (; i + N <= numPixels; i += N) {
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
size_t ProcessImage3DSIMD_Tetrahedral_Bulk(const LutData3D& lut, float* data, size_t numPixels) {
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

    size_t i = 0;
    for (; i + N <= numPixels; i += N) {
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

} // HWY_NAMESPACE
} // namespace cubelut
HWY_AFTER_NAMESPACE();

// ====== ONCE BLOCK ======
#if HWY_ONCE
namespace cubelut {

HWY_EXPORT(ProcessImage3DSIMD_Trilinear_Bulk);
HWY_EXPORT(ProcessImage3DSIMD_Tetrahedral_Bulk);

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

void Processor::processImage(const Lut& lut, float* data, size_t width, size_t height, Interpolation interp) {
    if (!lut.isValid() || !data) return;
    
    size_t numPixels = width * height;
    
    if (lut.shaper1D.has_value()) {
        const auto& shaper = *lut.shaper1D;
        // Simple scalar loop for 1D shaper for now
        for (size_t i = 0; i < numPixels; ++i) {
            std::array<float, 3> pixel = {data[i * 3], data[i * 3 + 1], data[i * 3 + 2]};
            auto result = process1D(shaper, pixel);
            data[i * 3] = result[0];
            data[i * 3 + 1] = result[1];
            data[i * 3 + 2] = result[2];
        }
    }
    
    if (lut.grid3D.has_value()) {
        size_t i = 0;
        const auto& grid = *lut.grid3D;
        if (interp == Interpolation::Tetrahedral) {
            i = HWY_DYNAMIC_DISPATCH(ProcessImage3DSIMD_Tetrahedral_Bulk)(grid, data, numPixels);
        } else {
            i = HWY_DYNAMIC_DISPATCH(ProcessImage3DSIMD_Trilinear_Bulk)(grid, data, numPixels);
        }
        
        // Tail pixels
        for (; i < numPixels; ++i) {
            std::array<float, 3> pixel = {data[i * 3], data[i * 3 + 1], data[i * 3 + 2]};
            auto result = (interp == Interpolation::Tetrahedral) ? process3DTetrahedral(grid, pixel) : process3DTrilinear(grid, pixel);
            data[i * 3] = result[0];
            data[i * 3 + 1] = result[1];
            data[i * 3 + 2] = result[2];
        }
    }
}

} // namespace cubelut
#endif
