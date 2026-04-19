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
        std::cout << "Type: " << (lut.type == cubelut::LutType::Lut1D ? "1D" : "3D") << std::endl;
        std::cout << "Size: " << lut.size << std::endl;
        std::cout << "Data points: " << lut.data.size() / 3 << std::endl;
        
        if (lut.data.size() >= 3) {
            std::cout << "First point: (" << lut.data[0] << ", " << lut.data[1] << ", " << lut.data[2] << ")" << std::endl;
        }
    } else {
        std::cerr << "Failed to parse LUT from " << filePath << std::endl;
    }

    return 0;
}
