#include <iostream>
#include "cubelut/cubelut.hpp"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <path_to_cube_file>" << std::endl;
        return 1;
    }

    std::string filePath = argv[1];
    auto lutOpt = cubelut::Parser::fromFile(filePath);

    if (lutOpt) {
        const auto& lut = *lutOpt;
        std::cout << "Successfully parsed LUT: " << lut.title << std::endl;
        std::cout << "Has 1D Shaper: " << (lut.shaper1D ? "Yes" : "No") << std::endl;
        std::cout << "Has 3D Grid: " << (lut.grid3D ? "Yes" : "No") << std::endl;

        if (lut.grid3D) {
            std::cout << "3D Grid Size: " << lut.grid3D->size << std::endl;
            std::cout << "3D Data points: " << lut.grid3D->data.size() / 3 << std::endl;
            if (lut.grid3D->data.size() >= 3) {
                std::cout << "First point: (" << lut.grid3D->data[0] << ", " << lut.grid3D->data[1] << ", " << lut.grid3D->data[2] << ")" << std::endl;
            }
        }    } else {
        std::cerr << "Failed to parse LUT from " << filePath << std::endl;
    }

    return 0;
}
