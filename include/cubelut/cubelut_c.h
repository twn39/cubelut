#pragma once

#include <stddef.h>
#include <stdbool.h>

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
 * Returns true if the LUT is 3D, false if 1D.
 */
bool cubelut_is_3d(const cubelut_lut_t* lut);

/**
 * Returns the size of the LUT grid (e.g., 33 for a 33x33x33 3D LUT, or 4096 for a 1D LUT).
 */
int cubelut_get_size(const cubelut_lut_t* lut);

/**
 * Gets the title of the LUT. The returned pointer is owned by the LUT object and
 * should not be freed. It is valid as long as the LUT object is valid.
 */
const char* cubelut_get_title(const cubelut_lut_t* lut);

/**
 * Creates and returns a new buffer containing the LUT data formatted as RGBA float32.
 * The original RGB data is padded with Alpha = 1.0f.
 * This format is ideal for direct upload to GPU textures (e.g., Metal MTLTexture, CoreImage).
 * 
 * @param lut The LUT object.
 * @param out_byte_size A pointer to a size_t where the total size in bytes of the returned buffer will be written.
 * @return A pointer to the newly allocated float array. The caller must free it using cubelut_free_buffer().
 */
float* cubelut_create_rgba_buffer(const cubelut_lut_t* lut, size_t* out_byte_size);

/**
 * Frees a buffer previously returned by cubelut_create_rgba_buffer.
 */
void cubelut_free_buffer(float* buffer);

#ifdef __cplusplus
}
#endif
