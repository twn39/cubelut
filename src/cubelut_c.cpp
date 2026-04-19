#include "cubelut/cubelut_c.h"
#include "cubelut/parser.hpp"
#include "cubelut/lut.hpp"
#include <cstring>
#include <optional>
#include <string>

// Hide the C++ implementation details within an opaque struct
struct cubelut_lut_t {
    cubelut::Lut inner;
};

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

bool cubelut_is_3d(const cubelut_lut_t* lut) {
    if (!lut) return false;
    return lut->inner.type == cubelut::LutType::Lut3D;
}

int cubelut_get_size(const cubelut_lut_t* lut) {
    if (!lut) return 0;
    return lut->inner.size;
}

const char* cubelut_get_title(const cubelut_lut_t* lut) {
    if (!lut) return "";
    return lut->inner.title.c_str();
}

float* cubelut_create_rgba_buffer(const cubelut_lut_t* lut, size_t* out_byte_size) {
    if (!lut || !out_byte_size) return nullptr;
    
    int size = lut->inner.size;
    size_t num_pixels = 0;
    
    if (lut->inner.type == cubelut::LutType::Lut1D) {
        num_pixels = static_cast<size_t>(size);
    } else {
        num_pixels = static_cast<size_t>(size) * size * size;
    }
    
    // 4 floats per pixel (RGBA)
    *out_byte_size = num_pixels * 4 * sizeof(float);
    float* rgba = new float[num_pixels * 4];
    
    const float* rgb = lut->inner.data.data();
    
    // Convert tight RGB array to RGBA padded to 1.0f.
    // Sequential iteration is very fast due to hardware memory prefetchers.
    for (size_t i = 0; i < num_pixels; ++i) {
        rgba[i * 4 + 0] = rgb[i * 3 + 0]; // R
        rgba[i * 4 + 1] = rgb[i * 3 + 1]; // G
        rgba[i * 4 + 2] = rgb[i * 3 + 2]; // B
        rgba[i * 4 + 3] = 1.0f;           // A
    }
    
    return rgba;
}

void cubelut_free_buffer(float* buffer) {
    delete[] buffer;
}

} // extern "C"
