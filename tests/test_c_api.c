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
    
    bool is_3d = cubelut_is_3d(lut);
    printf("Type: %s\n", is_3d ? "3D" : "1D");
    
    int size = cubelut_get_size(lut);
    printf("Size: %d\n", size);

    if (size != 33 || !is_3d) {
        fprintf(stderr, "Error: Unexpected LUT properties.\n");
        cubelut_free(lut);
        return 1;
    }

    size_t rgba_byte_size = 0;
    float* rgba_data = cubelut_create_rgba_buffer(lut, &rgba_byte_size);

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

    // Clean up
    cubelut_free_buffer(rgba_data);
    cubelut_free(lut);

    printf("--- Test Passed Successfully! ---\n");
    return 0;
}
