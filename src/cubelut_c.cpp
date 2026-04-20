#include "cubelut/cubelut_c.h"
#include "cubelut/parser.hpp"
#include "cubelut/processor.hpp"
#include "cubelut/lut.hpp"
#include <cstring>
#include <optional>
#include <string>

// Hide the C++ implementation details within an opaque struct
struct cubelut_lut_t {
    cubelut::Lut inner;
};

// Fast float-to-half conversion from rygorous (public domain)
// https://gist.github.com/rygorous/2156668
static inline uint16_t float_to_half(float v) {
    union FP32 {
        uint32_t u;
        float f;
    };
    FP32 f;
    f.f = v;
    FP32 f32infty = { 255 << 23 };
    FP32 f16max = { (127 + 16) << 23 };
    FP32 denorm_magic = { ((127 - 15) + (23 - 10) + 1) << 23 };
    uint32_t sign_mask = 0x80000000u;
    uint16_t o = 0;

    uint32_t sign = f.u & sign_mask;
    f.u ^= sign;

    // NOTE all the integer compares in this function can be safely
    // compiled into signed compares since all operands are below
    // 0x80000000. Important if you want fast straight SSE2 code
    // (since there's no unsigned PCMPGTD).

    if (f.u >= f16max.u) { // result is Inf or NaN (all exponent bits set)
        o = (f.u > f32infty.u) ? 0x7e00 : 0x7c00; // NaN->qNaN and Inf->Inf
    } else { // (De)normalized number or zero
        if (f.u < (113 << 23)) { // resulting FP16 is subnormal or zero
            // use a magic value to align our 10 mantissa bits at the bottom of
            // the float. as long as FP addition is round-to-nearest-even this
            // just works.
            f.f += denorm_magic.f;

            // and one integer subtract of the bias later, we have our final float!
            o = (uint16_t)(f.u - denorm_magic.u);
        } else {
            uint32_t mant_odd = (f.u >> 13) & 1; // resulting mantissa is odd

            // update exponent, rounding bias part 1
            f.u += (static_cast<uint32_t>(15 - 127) << 23) + 0xfff;
            // rounding bias part 2
            f.u += mant_odd;
            // take the bits!
            o = (uint16_t)(f.u >> 13);
        }
    }

    return o | (sign >> 16);
}

extern "C" {

cubelut_lut_t* cubelut_load_from_file(const char* file_path) {
    if (!file_path) return nullptr;
    
    std::optional<cubelut::Lut> result = cubelut::Parser::fromFile(file_path);
    if (!result || !result->isValid()) {
        return nullptr;
    }
    
    return new cubelut_lut_t{std::move(*result)};
}

cubelut_lut_t* cubelut_load_from_string(const char* content) {
    if (!content) return nullptr;
    
    std::optional<cubelut::Lut> result = cubelut::Parser::fromString(content);
    if (!result || !result->isValid()) {
        return nullptr;
    }
    
    return new cubelut_lut_t{std::move(*result)};
}

void cubelut_free(cubelut_lut_t* lut) {
    delete lut;
}

bool cubelut_has_shaper1d(const cubelut_lut_t* lut) {
    if (!lut) return false;
    return lut->inner.shaper1D.has_value() && lut->inner.shaper1D->isValid();
}

bool cubelut_has_grid3d(const cubelut_lut_t* lut) {
    if (!lut) return false;
    return lut->inner.grid3D.has_value() && lut->inner.grid3D->isValid();
}

int cubelut_get_shaper1d_size(const cubelut_lut_t* lut) {
    if (!lut || !cubelut_has_shaper1d(lut)) return 0;
    return lut->inner.shaper1D->size;
}

int cubelut_get_grid3d_size(const cubelut_lut_t* lut) {
    if (!lut || !cubelut_has_grid3d(lut)) return 0;
    return lut->inner.grid3D->size;
}

const char* cubelut_get_title(const cubelut_lut_t* lut) {
    if (!lut) return "";
    return lut->inner.title.c_str();
}

float* cubelut_create_rgba_buffer_for_grid3d(const cubelut_lut_t* lut, size_t* out_byte_size) {
    if (!lut || !out_byte_size || !cubelut_has_grid3d(lut)) return nullptr;
    
    int size = lut->inner.grid3D->size;
    size_t num_pixels = static_cast<size_t>(size) * size * size;
    
    // 4 floats per pixel (RGBA)
    *out_byte_size = num_pixels * 4 * sizeof(float);
    float* rgba = new float[num_pixels * 4];
    
    const float* rgb = lut->inner.grid3D->data.data();
    
    for (size_t i = 0; i < num_pixels; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0]; // R
        rgba[i * 4 + 1] = rgb[i * 3 + 1]; // G
        rgba[i * 4 + 2] = rgb[i * 3 + 2]; // B
        rgba[i * 4 + 3] = 1.0f;           // A
    }
    
    return rgba;
}

uint16_t* cubelut_create_rgba16_buffer_for_grid3d(const cubelut_lut_t* lut, size_t* out_byte_size) {
    if (!lut || !out_byte_size || !cubelut_has_grid3d(lut)) return nullptr;
    
    int size = lut->inner.grid3D->size;
    size_t num_pixels = static_cast<size_t>(size) * size * size;
    
    *out_byte_size = num_pixels * 4 * sizeof(uint16_t);
    uint16_t* rgba16 = new uint16_t[num_pixels * 4];
    
    const float* rgb32 = lut->inner.grid3D->data.data();
    const uint16_t alpha_1_fp16 = 0x3C00;
    
    for (size_t i = 0; i < num_pixels; ++i) {
        rgba16[i * 4 + 0] = float_to_half(rgb32[i * 3 + 0]); // R
        rgba16[i * 4 + 1] = float_to_half(rgb32[i * 3 + 1]); // G
        rgba16[i * 4 + 2] = float_to_half(rgb32[i * 3 + 2]); // B
        rgba16[i * 4 + 3] = alpha_1_fp16;                    // A
    }
    
    return rgba16;
}

const float* cubelut_get_raw_rgb_data_for_grid3d(const cubelut_lut_t* lut, size_t* out_num_floats) {
    if (!lut || !cubelut_has_grid3d(lut)) return nullptr;
    if (out_num_floats) {
        *out_num_floats = lut->inner.grid3D->data.size();
    }
    return lut->inner.grid3D->data.data();
}

const float* cubelut_get_raw_rgb_data_for_shaper1d(const cubelut_lut_t* lut, size_t* out_num_floats) {
    if (!lut || !cubelut_has_shaper1d(lut)) return nullptr;
    if (out_num_floats) {
        *out_num_floats = lut->inner.shaper1D->data.size();
    }
    return lut->inner.shaper1D->data.data();
}

void cubelut_free_buffer(void* buffer) {
    // Both float* and uint16_t* are allocated using new[]
    delete[] static_cast<char*>(buffer);
}

void cubelut_process_image_chunk(
    const cubelut_lut_t* lut, 
    float* image_rgb_data, 
    size_t start_pixel_idx, 
    size_t end_pixel_idx,
    bool use_tetrahedral
) {
    if (!lut || !image_rgb_data) return;
    
    cubelut::Interpolation interp = use_tetrahedral ? cubelut::Interpolation::Tetrahedral : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processPixels(lut->inner, image_rgb_data, start_pixel_idx, end_pixel_idx, interp);
}

} // extern "C"
