#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for the LUT object
typedef struct cubelut_lut_t cubelut_lut_t;

/**
 * Parses a .cube file and creates a LUT object.
 * Returns NULL if parsing fails or the file is not found.
 */
cubelut_lut_t* cubelut_load_from_file(const char* file_path);

/**
 * Parses a .cube content string and creates a LUT object.
 * Returns NULL if parsing fails.
 */
cubelut_lut_t* cubelut_load_from_string(const char* content);

/**
 * Frees the LUT object.
 */
void cubelut_free(cubelut_lut_t* lut);

/**
 * Returns true if the LUT pipeline contains a 1D shaper LUT.
 */
bool cubelut_has_shaper1d(const cubelut_lut_t* lut);

/**
 * Returns true if the LUT pipeline contains a 3D grid LUT.
 */
bool cubelut_has_grid3d(const cubelut_lut_t* lut);

/**
 * Returns the size of the 1D shaper LUT (0 if not present).
 */
int cubelut_get_shaper1d_size(const cubelut_lut_t* lut);

/**
 * Returns the size of the 3D grid LUT (0 if not present, e.g., 33 for 33x33x33).
 */
int cubelut_get_grid3d_size(const cubelut_lut_t* lut);

/**
 * Gets the title of the LUT. The returned pointer is owned by the LUT object and
 * should not be freed. It is valid as long as the LUT object is valid.
 */
const char* cubelut_get_title(const cubelut_lut_t* lut);

/**
 * Creates and returns a new buffer containing the 3D LUT data formatted as RGBA float32.
 * The original RGB data is padded with Alpha = 1.0f.
 * 
 * @param lut The LUT object.
 * @param out_byte_size Returns the total size in bytes of the returned buffer.
 * @return A pointer to the newly allocated float array. Caller must free using cubelut_free_buffer().
 */
float* cubelut_create_rgba_buffer_for_grid3d(const cubelut_lut_t* lut, size_t* out_byte_size);

/**
 * Creates and returns a new buffer containing the 3D LUT data formatted as RGBA float16 (half precision).
 * 
 * @param lut The LUT object.
 * @param out_byte_size Returns the total size in bytes of the returned buffer.
 * @return A pointer to the newly allocated uint16_t array. Caller must free using cubelut_free_buffer().
 */
uint16_t* cubelut_create_rgba16_buffer_for_grid3d(const cubelut_lut_t* lut, size_t* out_byte_size);

/**
 * Gets a direct, zero-copy pointer to the internal RGB floating-point data for the 3D Grid.
 * 
 * @param lut The LUT object.
 * @param out_num_floats Returns the total number of floats.
 * @return A constant pointer to the internal float array.
 */
const float* cubelut_get_raw_rgb_data_for_grid3d(const cubelut_lut_t* lut, size_t* out_num_floats);

/**
 * Gets a direct, zero-copy pointer to the internal RGB floating-point data for the 1D Shaper.
 * 
 * @param lut The LUT object.
 * @param out_num_floats Returns the total number of floats.
 * @return A constant pointer to the internal float array.
 */
const float* cubelut_get_raw_rgb_data_for_shaper1d(const cubelut_lut_t* lut, size_t* out_num_floats);

/**
 * Frees a buffer previously returned by cubelut_create_rgba_buffer or cubelut_create_rgba16_buffer.
 */
void cubelut_free_buffer(void* buffer);

/**
 * Processes a specific chunk of the image in-place. 
 * This is designed for multi-threading (Thread-Level Parallelism).
 * The host application can split the image into N chunks and submit them to its own thread pool.
 * 
 * @param lut The LUT object pipeline.
 * @param image_rgb_data A flat array of tightly-packed RGB floats (0.0 to 1.0).
 *                       It must contain at least (end_pixel_idx * 3) elements.
 * @param start_pixel_idx The starting pixel index (inclusive).
 * @param end_pixel_idx The ending pixel index (exclusive).
 * @param use_tetrahedral Set to true to use Tetrahedral interpolation (higher quality), 
 *                        false for Trilinear.
 */
void cubelut_process_image_chunk(
    const cubelut_lut_t* lut, 
    float* image_rgb_data, 
    size_t start_pixel_idx, 
    size_t end_pixel_idx,
    bool use_tetrahedral
);

#ifdef __cplusplus
}
#endif
