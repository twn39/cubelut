#include "cubelut/cubelut_c.h"
#include "cubelut/parser.hpp"
#include "cubelut/processor.hpp"
#include "cubelut/lut.hpp"
#include "cubelut/baker.hpp"
#include "cubelut/writer.hpp"
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

    *out_byte_size = num_pixels * 4 * sizeof(float);
    float* rgba = new float[num_pixels * 4];

    // SIMD-accelerated RGB float32 → RGBA float32 expansion.
    cubelut::Processor::convertRGBToRGBA32(lut->inner.grid3D->data.data(), rgba, num_pixels);

    return rgba;
}

uint16_t* cubelut_create_rgba16_buffer_for_grid3d(const cubelut_lut_t* lut, size_t* out_byte_size) {
    if (!lut || !out_byte_size || !cubelut_has_grid3d(lut)) return nullptr;

    int size = lut->inner.grid3D->size;
    size_t num_pixels = static_cast<size_t>(size) * size * size;

    *out_byte_size = num_pixels * 4 * sizeof(uint16_t);
    uint16_t* rgba16 = new uint16_t[num_pixels * 4];

    // SIMD-accelerated RGB float32 → RGBA float16 conversion via Highway DemoteTo.
    cubelut::Processor::convertRGBToRGBA16(lut->inner.grid3D->data.data(), rgba16, num_pixels);

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
    // Both float* and uint16_t* are allocated using new[].
    // Use ::operator delete[] to avoid UB from type-punning through char*.
    ::operator delete[](buffer);
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

// Helper: write a float value to an optional out-pointer.
static inline void assign_if(float* ptr, float v) { if (ptr) *ptr = v; }

bool cubelut_get_grid3d_domain(const cubelut_lut_t* lut,
                                float* min_r, float* min_g, float* min_b,
                                float* max_r, float* max_g, float* max_b) {
    if (!lut || !cubelut_has_grid3d(lut)) return false;
    const auto& dom = lut->inner.grid3D->domain;
    assign_if(min_r, dom.min[0]); assign_if(min_g, dom.min[1]); assign_if(min_b, dom.min[2]);
    assign_if(max_r, dom.max[0]); assign_if(max_g, dom.max[1]); assign_if(max_b, dom.max[2]);
    return true;
}

bool cubelut_get_shaper1d_domain(const cubelut_lut_t* lut,
                                  float* min_r, float* min_g, float* min_b,
                                  float* max_r, float* max_g, float* max_b) {
    if (!lut || !cubelut_has_shaper1d(lut)) return false;
    const auto& dom = lut->inner.shaper1D->domain;
    assign_if(min_r, dom.min[0]); assign_if(min_g, dom.min[1]); assign_if(min_b, dom.min[2]);
    assign_if(max_r, dom.max[0]); assign_if(max_g, dom.max[1]); assign_if(max_b, dom.max[2]);
    return true;
}

} // extern "C"

// ============================================================================
// Writer C API implementation
// ============================================================================

extern "C" {

bool cubelut_write_to_file(const cubelut_lut_t* lut, const char* file_path) {
    if (!lut || !file_path) return false;
    return cubelut::Writer::toFile(lut->inner, file_path).ok();
}

char* cubelut_write_to_string(const cubelut_lut_t* lut) {
    if (!lut) return nullptr;
    std::string s = cubelut::Writer::toString(lut->inner);
    if (s.empty()) return nullptr;
    // Allocated with new char[] — caller frees via cubelut_free_buffer()
    char* buf = new char[s.size() + 1];
    std::memcpy(buf, s.data(), s.size() + 1);
    return buf;
}

// ============================================================================
// Baker C API implementation
// ============================================================================

float* cubelut_bake_identity_grid3d(int size, size_t* out_num_floats) {
    if (size < 2) return nullptr;
    cubelut::LutData3D grid = cubelut::Baker::makeIdentity3D(size);
    const size_t n = grid.data.size();
    if (out_num_floats) *out_num_floats = n;
    float* buf = new float[n];
    std::memcpy(buf, grid.data.data(), n * sizeof(float));
    return buf;
}

// ============================================================================
// Parallel Processing C API implementation
// ============================================================================

cubelut_pixel_chunk_t* cubelut_get_parallel_chunks(
    size_t width, size_t height, unsigned num_threads, size_t* out_count)
{
    auto chunks = cubelut::Processor::getParallelChunks(width, height, num_threads);
    if (chunks.empty()) {
        if (out_count) *out_count = 0;
        return nullptr;
    }
    const size_t n = chunks.size();
    if (out_count) *out_count = n;
    auto* buf = new cubelut_pixel_chunk_t[n];
    for (size_t i = 0; i < n; ++i) {
        buf[i].start_pixel = chunks[i].startPixel;
        buf[i].end_pixel   = chunks[i].endPixel;
    }
    return buf;  // Caller frees with cubelut_free_buffer()
}

void cubelut_process_pixels(
    const cubelut_lut_t* lut, float* data,
    size_t start_pixel, size_t end_pixel, bool use_tetrahedral)
{
    if (!lut || !data) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral
        : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processPixels(lut->inner, data, start_pixel, end_pixel, interp);
}

void cubelut_process_image_parallel(
    const cubelut_lut_t* lut, float* data,
    size_t width, size_t height, bool use_tetrahedral, unsigned num_threads)
{
    if (!lut || !data) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral
        : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processImageParallel(
        lut->inner, data, width, height, interp, num_threads);
}

// ── uint8 C API ───────────────────────────────────────────────────────────────

void cubelut_process_image_u8(
    const cubelut_lut_t* lut, const uint8_t* input, uint8_t* output,
    size_t width, size_t height, bool use_tetrahedral)
{
    if (!lut || !input || !output) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral
        : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processImageU8(lut->inner, input, output, width, height, interp);
}

void cubelut_process_image_u8_parallel(
    const cubelut_lut_t* lut, const uint8_t* input, uint8_t* output,
    size_t width, size_t height, bool use_tetrahedral, unsigned num_threads)
{
    if (!lut || !input || !output) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral
        : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processImageU8Parallel(
        lut->inner, input, output, width, height, interp, num_threads);
}

void cubelut_process_image_rgba8(
    const cubelut_lut_t* lut, const uint8_t* input, uint8_t* output,
    size_t width, size_t height, bool use_tetrahedral)
{
    if (!lut || !input || !output) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral
        : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processImageRGBA8(lut->inner, input, output, width, height, interp);
}

void cubelut_process_image_rgba8_parallel(
    const cubelut_lut_t* lut, const uint8_t* input, uint8_t* output,
    size_t width, size_t height, bool use_tetrahedral, unsigned num_threads)
{
    if (!lut || !input || !output) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral
        : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processImageRGBA8Parallel(
        lut->inner, input, output, width, height, interp, num_threads);
}

// ── PixelLayout C API ─────────────────────────────────────────────────────────

static cubelut::PixelLayout to_pixel_layout(cubelut_layout_t l) {
    switch (l) {
    case CUBELUT_LAYOUT_RGBA_F32: return cubelut::PixelLayout::RGBA_F32;
    case CUBELUT_LAYOUT_BGR_F32:  return cubelut::PixelLayout::BGR_F32;
    case CUBELUT_LAYOUT_BGRA_F32: return cubelut::PixelLayout::BGRA_F32;
    default:                      return cubelut::PixelLayout::RGB_F32;
    }
}

void cubelut_process_image_ex(
    const cubelut_lut_t* lut, float* data,
    size_t width, size_t height,
    cubelut_layout_t layout, bool use_tetrahedral)
{
    if (!lut || !data) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processImage(lut->inner, data, width, height,
                                     to_pixel_layout(layout), interp);
}

void cubelut_process_image_ex_parallel(
    const cubelut_lut_t* lut, float* data,
    size_t width, size_t height,
    cubelut_layout_t layout, bool use_tetrahedral, unsigned num_threads)
{
    if (!lut || !data) return;
    const auto interp = use_tetrahedral
        ? cubelut::Interpolation::Tetrahedral : cubelut::Interpolation::Trilinear;
    cubelut::Processor::processImageParallel(lut->inner, data, width, height,
                                              to_pixel_layout(layout), interp, num_threads);
}

// ── Comment API ───────────────────────────────────────────────────────────────

size_t cubelut_get_comment_count(const cubelut_lut_t* lut) {
    if (!lut) return 0;
    return lut->inner.comments.size();
}

const char* cubelut_get_comment(const cubelut_lut_t* lut, size_t index) {
    if (!lut || index >= lut->inner.comments.size()) return nullptr;
    return lut->inner.comments[index].c_str();
}

void cubelut_add_comment(cubelut_lut_t* lut, const char* text) {
    if (!lut) return;
    lut->inner.comments.push_back(text ? std::string(text) : std::string{});
}

void cubelut_clear_comments(cubelut_lut_t* lut) {
    if (!lut) return;
    lut->inner.comments.clear();
}

} // extern "C"
