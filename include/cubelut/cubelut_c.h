#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle for the LUT object
typedef struct cubelut_lut_t cubelut_lut_t;

// ---------------------------------------------------------------------------
// Structured parse errors (thread-local last-error after a failed load)
// ---------------------------------------------------------------------------

typedef enum {
    CUBELUT_PARSE_OK = 0,
    CUBELUT_PARSE_FILE_NOT_FOUND,
    CUBELUT_PARSE_FILE_READ_ERROR,
    CUBELUT_PARSE_MISSING_LUT_SIZE,
    CUBELUT_PARSE_INVALID_LUT_SIZE,
    CUBELUT_PARSE_INVALID_DOMAIN,
    CUBELUT_PARSE_INSUFFICIENT_DATA,
    CUBELUT_PARSE_UNKNOWN
} cubelut_parse_error_t;

/**
 * Last error code from cubelut_load_from_file / cubelut_load_from_string
 * on the calling thread. CUBELUT_PARSE_OK after a successful load.
 */
cubelut_parse_error_t cubelut_get_last_error(void);

/**
 * Human-readable detail for the last load error (never NULL).
 * Valid until the next load call on the same thread.
 */
const char* cubelut_get_last_error_message(void);

/**
 * Parses a .cube file and creates a LUT object.
 * Returns NULL on failure; inspect cubelut_get_last_error().
 */
cubelut_lut_t* cubelut_load_from_file(const char* file_path);

/**
 * Parses a .cube content string and creates a LUT object.
 * Returns NULL on failure; inspect cubelut_get_last_error().
 */
        cubelut_lut_t* cubelut_load_from_string(const char* content);

/**
 * Builds a 3D-only LUT from an RGB lattice (Iridas .cube order: blue-major, red-fastest).
 * `rgb_count` must equal size³ × 3. Returns NULL on failure; inspect cubelut_get_last_error().
 */
cubelut_lut_t* cubelut_load_from_grid3d(
    int size,
    const float* rgb,
    size_t rgb_count,
    float min_r, float min_g, float min_b,
    float max_r, float max_g, float max_b
);

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
 *
 * @note This exports the **raw 3D grid lattice only**. It does **not** apply a 1D shaper
 *       or non-unit DOMAIN. Prefer cubelut_create_rgba16_for_gpu_export() for Metal/Vulkan.
 */
uint16_t* cubelut_create_rgba16_buffer_for_grid3d(const cubelut_lut_t* lut, size_t* out_byte_size);

/**
 * Returns true when a raw grid3d upload would NOT match CPU Processor::process()
 * for unit-cube lattice coordinates [0,1]³.
 *
 * True when:
 *   - a 1D shaper is present, or
 *   - 3D DOMAIN_MIN/MAX is not approximately [0,1], or
 *   - there is no 3D grid (shaper-only; must bake to 3D for GPU)
 */
bool cubelut_needs_gpu_pipeline_bake(const cubelut_lut_t* lut);

/**
 * Grid size that cubelut_create_rgba16_for_gpu_export() will produce.
 * Prefers native 3D size; falls back to 33 when only a 1D shaper exists.
 * Returns 0 if the LUT is NULL or invalid for GPU export.
 */
int cubelut_gpu_export_grid_size(const cubelut_lut_t* lut);

/**
 * Builds an RGBA16Float 3D texture payload for GPU tetrahedral sampling under the
 * Espresso/Metal contract: sample coordinates in linear [0,1]³ map to the lattice.
 *
 * When cubelut_needs_gpu_pipeline_bake() is true, samples the full CPU pipeline
 * (shaper + domain + 3D) onto an identity lattice, then packs FP16.
 * Otherwise packs the native 3D grid (same as cubelut_create_rgba16_buffer_for_grid3d).
 *
 * @param out_byte_size  Total bytes of returned buffer (size³ × 4 × 2).
 * @param out_grid_size  Optional; receives lattice dimension N (texture width/height/depth).
 * @return Heap buffer; free with cubelut_free_buffer(). NULL on failure.
 */
uint16_t* cubelut_create_rgba16_for_gpu_export(
    const cubelut_lut_t* lut,
    size_t* out_byte_size,
    int* out_grid_size
);

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

/**
 * Queries the input domain of the 3D Grid LUT.
 * On return, the six out-pointers receive [min_r, min_g, min_b] and
 * [max_r, max_g, max_b].  Any out-pointer may be NULL if not needed.
 * Returns false if the LUT has no 3D grid or if `lut` is NULL.
 */
bool cubelut_get_grid3d_domain(const cubelut_lut_t* lut,
                                float* min_r, float* min_g, float* min_b,
                                float* max_r, float* max_g, float* max_b);

/**
 * Queries the input domain of the 1D Shaper LUT.
 * Same semantics as cubelut_get_grid3d_domain.
 * Returns false if the LUT has no 1D shaper or if `lut` is NULL.
 */
bool cubelut_get_shaper1d_domain(const cubelut_lut_t* lut,
                                  float* min_r, float* min_g, float* min_b,
                                  float* max_r, float* max_g, float* max_b);

// ============================================================================
// Writer API
// ============================================================================

/**
 * Serialize `lut` to a .cube file at `file_path`.
 * Returns true on success, false on any error (invalid LUT, bad path, I/O).
 */
bool cubelut_write_to_file(const cubelut_lut_t* lut, const char* file_path);

/**
 * Serialize `lut` to a heap-allocated, NUL-terminated C string.
 * The caller MUST free the returned pointer with cubelut_free_buffer().
 * Returns NULL if `lut` is NULL or invalid.
 */
char* cubelut_write_to_string(const cubelut_lut_t* lut);

// ============================================================================
// Baker API
// ============================================================================

/**
 * Generate an identity 3D LUT grid of `size`×`size`×`size` lattice points.
 * Layout: RGB float triplets, Blue-major / Red-fastest (.cube canonical order).
 * *out_num_floats is set to size³×3.
 * The caller MUST free the returned pointer with cubelut_free_buffer().
 * Returns NULL if size < 2.
 */
float* cubelut_bake_identity_grid3d(int size, size_t* out_num_floats);

// ============================================================================
// Parallel Processing API
// ============================================================================

/**
 * Descriptor for one SIMD/cache-aligned pixel range.
 * Generated by cubelut_get_parallel_chunks() for use with any thread pool.
 *
 * Alignment guarantee: start_pixel is always a multiple of 16 pixels (192 bytes),
 * which simultaneously satisfies:
 *   - AVX-512 SIMD lane alignment (no scalar head processing per chunk)
 *   - Cache-line false-sharing safety (LCM(64-byte line, 12-byte RGB) = 192 bytes)
 */
typedef struct {
    size_t start_pixel; /**< Inclusive start pixel index (multiple of 16). */
    size_t end_pixel;   /**< Exclusive end pixel index. */
} cubelut_pixel_chunk_t;

/**
 * Compute cache-line and SIMD-aligned pixel chunks for parallel dispatch.
 *
 * The returned array has *out_count elements and MUST be freed with
 * cubelut_free_buffer(). Returns NULL on invalid input.
 *
 * Example (Swift + GCD):
 *   var count = 0
 *   let chunks = cubelut_get_parallel_chunks(3840, 2160, 0, &count)!
 *   DispatchQueue.concurrentPerform(iterations: count) { i in
 *       cubelut_process_pixels(lut, data, chunks[i].start_pixel, chunks[i].end_pixel, true)
 *   }
 *   cubelut_free_buffer(chunks)
 *
 * num_threads: 0 = hardware_concurrency()
 */
cubelut_pixel_chunk_t* cubelut_get_parallel_chunks(
    size_t width,
    size_t height,
    unsigned num_threads,
    size_t* out_count
);

/**
 * Apply the LUT to pixels [start_pixel, end_pixel) in-place.
 * Thread-safe for non-overlapping ranges: safe to call concurrently from
 * multiple threads with different, non-overlapping [start, end) ranges.
 */
void cubelut_process_pixels(
    const cubelut_lut_t* lut,
    float* image_rgb_data,
    size_t start_pixel,
    size_t end_pixel,
    bool use_tetrahedral
);

/**
 * Parallel image processing with platform-adaptive dispatch:
 *   - Apple: uses dispatch_apply() (GCD pre-warmed pool, ~1 µs overhead)
 *   - Other: uses std::async                           (~120 µs overhead)
 *
 * num_threads: 0 = platform default (hardware_concurrency)
 */
void cubelut_process_image_parallel(
    const cubelut_lut_t* lut,
    float* image_rgb_data,
    size_t width,
    size_t height,
    bool use_tetrahedral,
    unsigned num_threads
);

// ============================================================================
// uint8 Image Processing API
// ============================================================================

/// Apply LUT to a uint8 RGB image (3 bytes/pixel, tightly packed).
/// input and output may be equal for in-place processing.
/// Internally uses 4096-pixel float32 chunks (L2-hot); no heap allocation.
void cubelut_process_image_u8(
    const cubelut_lut_t* lut,
    const uint8_t* input,
    uint8_t* output,
    size_t width,
    size_t height,
    bool use_tetrahedral
);

/// Parallel uint8 RGB processing (GCD on Apple, std::async elsewhere).
/// num_threads: 0 = hardware_concurrency()
void cubelut_process_image_u8_parallel(
    const cubelut_lut_t* lut,
    const uint8_t* input,
    uint8_t* output,
    size_t width,
    size_t height,
    bool use_tetrahedral,
    unsigned num_threads
);

/// Apply LUT to a uint8 RGBA image (4 bytes/pixel).
/// Alpha channel is passed through unchanged.
void cubelut_process_image_rgba8(
    const cubelut_lut_t* lut,
    const uint8_t* input,
    uint8_t* output,
    size_t width,
    size_t height,
    bool use_tetrahedral
);

/// Parallel RGBA8 processing.
void cubelut_process_image_rgba8_parallel(
    const cubelut_lut_t* lut,
    const uint8_t* input,
    uint8_t* output,
    size_t width,
    size_t height,
    bool use_tetrahedral,
    unsigned num_threads
);

// ============================================================================
// float32 PixelLayout API (single-pass SIMD, zero temp buffer)
// ============================================================================

/// Pixel memory layout for float32 image buffers.
typedef enum {
    CUBELUT_LAYOUT_RGB_F32  = 0, ///< 3×float32/pixel [R,G,B]
    CUBELUT_LAYOUT_RGBA_F32 = 1, ///< 4×float32/pixel [R,G,B,A] — alpha passthrough
    CUBELUT_LAYOUT_BGR_F32  = 2, ///< 3×float32/pixel [B,G,R]   — OpenCV default
    CUBELUT_LAYOUT_BGRA_F32 = 3, ///< 4×float32/pixel [B,G,R,A] — Metal BGRA32Float
} cubelut_layout_t;

/// Apply LUT to a float32 image with the given pixel layout (in-place).
/// Alpha channel is always passed through unchanged.
/// Dispatches the optimal SIMD kernel for the host CPU automatically.
void cubelut_process_image_ex(
    const cubelut_lut_t* lut,
    float*               data,
    size_t               width,
    size_t               height,
    cubelut_layout_t     layout,
    bool                 use_tetrahedral
);

/// Parallel version of cubelut_process_image_ex.
/// num_threads: 0 = hardware_concurrency()
void cubelut_process_image_ex_parallel(
    const cubelut_lut_t* lut,
    float*               data,
    size_t               width,
    size_t               height,
    cubelut_layout_t     layout,
    bool                 use_tetrahedral,
    unsigned             num_threads
);

// ============================================================================
// Comment API
// ============================================================================

/// Returns the number of comment lines stored in the LUT.
size_t cubelut_get_comment_count(const cubelut_lut_t* lut);

/// Returns the i-th comment line WITHOUT the leading "# " prefix.
/// Returns NULL if lut is NULL or index is out of range.
/// The returned pointer is valid until the LUT is freed or comments are modified.
const char* cubelut_get_comment(const cubelut_lut_t* lut, size_t index);

/// Appends one comment line to the LUT (without the "# " prefix).
/// An empty string appends a blank comment line.
void cubelut_add_comment(cubelut_lut_t* lut, const char* text);

/// Removes all comment lines from the LUT.
void cubelut_clear_comments(cubelut_lut_t* lut);

#ifdef __cplusplus
}
#endif
