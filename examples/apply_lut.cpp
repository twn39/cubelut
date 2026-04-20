#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "cubelut/cubelut.hpp"
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <lut.cube> <input_image> <output_image>\n";
        return 1;
    }

    std::string lutPath = argv[1];
    std::string inputPath = argv[2];
    std::string outputPath = argv[3];

    // Load LUT
    auto lutOpt = cubelut::Parser::fromFile(lutPath);
    if (!lutOpt) {
        std::cerr << "Failed to load LUT: " << lutPath << std::endl;
        return 1;
    }
    const auto& lut = *lutOpt;
    int size = lut.grid3D ? lut.grid3D->size : (lut.shaper1D ? lut.shaper1D->size : 0);
    std::cout << "Loaded LUT: " << lutPath << " (Size: " << size << ")" << std::endl;

    // Load Image
    int width, height, channels;
    unsigned char* img = stbi_load(inputPath.c_str(), &width, &height, &channels, 3); // Force 3 channels (RGB)
    if (!img) {
        std::cerr << "Failed to load image: " << inputPath << std::endl;
        return 1;
    }

    std::cout << "Loaded image: " << width << "x" << height << ", 3 channels (RGB)" << std::endl;

    // Convert image to float
    std::vector<float> imgFloat(width * height * 3);
    for (int i = 0; i < width * height * 3; ++i) {
        imgFloat[i] = img[i] / 255.0f;
    }

    // Apply LUT (Multi-threaded Chunking)
    std::cout << "Applying LUT using multi-threaded chunking..." << std::endl;
    
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;
    
    std::cout << "  Spawning " << num_threads << " worker threads..." << std::endl;
    
    std::vector<std::thread> threads;
    size_t total_pixels = width * height;
    size_t chunk_size = total_pixels / num_threads;
    
    for (unsigned int t = 0; t < num_threads; ++t) {
        size_t start_idx = t * chunk_size;
        size_t end_idx = (t == num_threads - 1) ? total_pixels : start_idx + chunk_size;
        
        threads.emplace_back([&lut, &imgFloat, start_idx, end_idx]() {
            cubelut::Processor::processPixels(lut, imgFloat.data(), start_idx, end_idx);
        });
    }
    
    for (auto& th : threads) {
        th.join();
    }

    // Convert back to unsigned char
    std::vector<unsigned char> imgOut(width * height * 3);
    for (int i = 0; i < width * height * 3; ++i) {
        float val = imgFloat[i] * 255.0f;
        // Clamp
        val = std::max(0.0f, std::min(255.0f, val));
        imgOut[i] = static_cast<unsigned char>(val);
    }

    // Save image
    int success = 0;
    // Simple string ends_with check (C++20 feature, but we can do it manually for C++17)
    auto ends_with = [](const std::string& str, const std::string& suffix) {
        return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
    };

    if (ends_with(outputPath, ".png")) {
        success = stbi_write_png(outputPath.c_str(), width, height, 3, imgOut.data(), width * 3);
    } else if (ends_with(outputPath, ".jpg") || ends_with(outputPath, ".jpeg")) {
        success = stbi_write_jpg(outputPath.c_str(), width, height, 3, imgOut.data(), 90);
    } else {
        std::cerr << "Unsupported output format. Use .png or .jpg" << std::endl;
    }

    stbi_image_free(img);

    if (!success) {
        std::cerr << "Failed to write output image: " << outputPath << std::endl;
        return 1;
    }

    std::cout << "Successfully applied LUT and saved to " << outputPath << std::endl;
    return 0;
}
