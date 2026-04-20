#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cubelut/cubelut_c.h"

int main(int argc, char** argv) {
    printf("--- Running cubelut C-API Test ---\n");

    const char* test_file = "../tests/files/FLog2C_to_ACROS_33grid_V.1.00.cube";
    printf("Loading LUT: %s\n", test_file);

    cubelut_lut_t* lut = cubelut_load_from_file(test_file);
    if (!lut) {
        fprintf(stderr, "Error: Failed to load LUT file.\n");
        return 1;
    }

    printf("LUT loaded successfully!\n");
    printf("Title: %s\n", cubelut_get_title(lut));
    
    bool has_1d = cubelut_has_shaper1d(lut);
    bool has_3d = cubelut_has_grid3d(lut);
    printf("Has 1D Shaper: %s\n", has_1d ? "Yes" : "No");
    printf("Has 3D Grid: %s\n", has_3d ? "Yes" : "No");
    
    int size = cubelut_get_grid3d_size(lut);
    printf("3D Grid Size: %d\n", size);

    if (size != 33 || !has_3d) {
        fprintf(stderr, "Error: Unexpected LUT properties.\n");
        cubelut_free(lut);
        return 1;
    }

    size_t rgba_byte_size = 0;
    float* rgba_data = cubelut_create_rgba_buffer_for_grid3d(lut, &rgba_byte_size);

    if (!rgba_data) {
        fprintf(stderr, "Error: Failed to create RGBA buffer.\n");
        cubelut_free(lut);
        return 1;
    }

    size_t expected_pixels = 33 * 33 * 33;
    size_t expected_bytes = expected_pixels * 4 * sizeof(float);

    printf("Generated RGBA Buffer:\n");
    printf("  Byte Size: %zu (Expected: %zu)\n", rgba_byte_size, expected_bytes);
    printf("  Total Pixels: %zu\n", expected_pixels);

    if (rgba_byte_size != expected_bytes) {
        fprintf(stderr, "Error: RGBA buffer size mismatch.\n");
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }

    // Inspect the first few pixels to ensure RGBA formatting (Alpha should be 1.0)
    printf("Inspecting first 3 pixels:\n");
    for (int i = 0; i < 3; ++i) {
        printf("  Pixel %d: R=%.4f, G=%.4f, B=%.4f, A=%.4f\n", 
               i, rgba_data[i*4 + 0], rgba_data[i*4 + 1], rgba_data[i*4 + 2], rgba_data[i*4 + 3]);
        
        if (rgba_data[i*4 + 3] != 1.0f) {
            fprintf(stderr, "Error: Alpha channel is not 1.0f at pixel %d.\n", i);
            cubelut_free_buffer(rgba_data);
            cubelut_free(lut);
            return 1;
        }
    }

    // Inspect the last pixel
    size_t last_idx = expected_pixels - 1;
    printf("Inspecting last pixel (Index %zu):\n", last_idx);
    printf("  Pixel %zu: R=%.4f, G=%.4f, B=%.4f, A=%.4f\n", 
           last_idx, 
           rgba_data[last_idx*4 + 0], 
           rgba_data[last_idx*4 + 1], 
           rgba_data[last_idx*4 + 2], 
           rgba_data[last_idx*4 + 3]);

    if (rgba_data[last_idx*4 + 3] != 1.0f) {
        fprintf(stderr, "Error: Alpha channel is not 1.0f at the last pixel.\n");
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }

    // --- Test Float16 Buffer Generation ---
    printf("\nGenerating RGBA16 (Float16) Buffer:\n");
    size_t rgba16_byte_size = 0;
    uint16_t* rgba16_data = cubelut_create_rgba16_buffer_for_grid3d(lut, &rgba16_byte_size);

    if (!rgba16_data) {
        fprintf(stderr, "Error: Failed to create RGBA16 buffer.\n");
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }

    size_t expected_16_bytes = expected_pixels * 4 * sizeof(uint16_t);
    printf("  Byte Size: %zu (Expected: %zu)\n", rgba16_byte_size, expected_16_bytes);

    if (rgba16_byte_size != expected_16_bytes || rgba16_byte_size != expected_bytes / 2) {
        fprintf(stderr, "Error: RGBA16 buffer size mismatch.\n");
        cubelut_free_buffer(rgba16_data);
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }

    printf("Inspecting first 3 pixels (Float16 Hex representation):\n");
    for (int i = 0; i < 3; ++i) {
        printf("  Pixel %d: R=0x%04X, G=0x%04X, B=0x%04X, A=0x%04X\n", 
               i, rgba16_data[i*4 + 0], rgba16_data[i*4 + 1], rgba16_data[i*4 + 2], rgba16_data[i*4 + 3]);
        
        if (rgba16_data[i*4 + 3] != 0x3C00) {
            fprintf(stderr, "Error: Float16 Alpha channel is not 0x3C00 at pixel %d.\n", i);
            cubelut_free_buffer(rgba16_data);
            cubelut_free_buffer(rgba_data);
            cubelut_free(lut);
            return 1;
        }
    }

    printf("Inspecting last pixel (Index %zu):\n", last_idx);
    printf("  Pixel %zu: R=0x%04X, G=0x%04X, B=0x%04X, A=0x%04X\n", 
           last_idx, 
           rgba16_data[last_idx*4 + 0], 
           rgba16_data[last_idx*4 + 1], 
           rgba16_data[last_idx*4 + 2], 
           rgba16_data[last_idx*4 + 3]);

    if (rgba16_data[last_idx*4 + 3] != 0x3C00) {
        fprintf(stderr, "Error: Float16 Alpha channel is not 0x3C00 at the last pixel.\n");
        cubelut_free_buffer(rgba16_data);
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }

    // --- Test Raw Zero-Copy Pointer Generation ---
    printf("\nTesting Raw Zero-Copy RGB Pointer:\n");
    size_t num_raw_floats = 0;
    const float* raw_rgb = cubelut_get_raw_rgb_data_for_grid3d(lut, &num_raw_floats);

    if (!raw_rgb) {
        fprintf(stderr, "Error: Failed to get raw RGB pointer.\n");
        cubelut_free_buffer(rgba16_data);
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }

    size_t expected_raw_floats = expected_pixels * 3;
    printf("  Total Floats: %zu (Expected: %zu)\n", num_raw_floats, expected_raw_floats);

    if (num_raw_floats != expected_raw_floats) {
        fprintf(stderr, "Error: Raw floats count mismatch.\n");
        cubelut_free_buffer(rgba16_data);
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }

    // Compare raw data vs buffered data
    printf("Comparing internal raw pointer with the extracted buffers...\n");
    for (int i = 0; i < 3; ++i) {
        if (raw_rgb[i*3 + 0] != rgba_data[i*4 + 0] || 
            raw_rgb[i*3 + 1] != rgba_data[i*4 + 1] || 
            raw_rgb[i*3 + 2] != rgba_data[i*4 + 2]) {
            fprintf(stderr, "Error: Data mismatch at pixel %d.\n", i);
            cubelut_free_buffer(rgba16_data);
            cubelut_free_buffer(rgba_data);
            cubelut_free(lut);
            return 1;
        }
    }
    printf("  Data verification passed.\n");
    
    // --- Test process chunk ---
    printf("\nTesting Process Image Chunk (Multi-threading readiness)...\n");
    float* dummy_img = (float*)malloc(4 * 3 * sizeof(float)); // 4 pixels
    for(int i=0; i<12; i++) dummy_img[i] = 0.5f;
    
    // Process only the middle 2 pixels (index 1 and 2)
    cubelut_process_image_chunk(lut, dummy_img, 1, 3, true);
    
    // Pixel 0 and 3 should be untouched
    if (dummy_img[0] != 0.5f || dummy_img[9] != 0.5f) {
        fprintf(stderr, "Error: Chunk processing leaked into unrequested pixels.\n");
        free(dummy_img);
        cubelut_free_buffer(rgba16_data);
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }
    
    // Pixel 1 and 2 should be modified (not 0.5f anymore)
    if (dummy_img[3] == 0.5f || dummy_img[6] == 0.5f) {
        fprintf(stderr, "Error: Chunk processing failed to modify requested pixels.\n");
        free(dummy_img);
        cubelut_free_buffer(rgba16_data);
        cubelut_free_buffer(rgba_data);
        cubelut_free(lut);
        return 1;
    }
    free(dummy_img);
    printf("  Chunk verification passed.\n");

    // Clean up
    cubelut_free_buffer(rgba16_data);
    cubelut_free_buffer(rgba_data);
    cubelut_free(lut);

    printf("--- Test Passed Successfully! ---\n");
    return 0;
}
