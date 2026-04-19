#include <iostream>
#include <cassert>
#include <cmath>
#include <vector>
#include "cubelut/cubelut.hpp"

void test_identity_1d() {
    cubelut::Lut lut;
    cubelut::LutData1D d1;
    d1.size = 2;
    d1.data = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f};
    lut.shaper1D = std::move(d1);
    
    std::array<float, 3> pixel = {0.5f, 0.5f, 0.5f};
    auto result = cubelut::Processor::process(lut, pixel);
    
    assert(std::abs(result[0] - 0.5f) < 1e-6);
    assert(std::abs(result[1] - 0.5f) < 1e-6);
    assert(std::abs(result[2] - 0.5f) < 1e-6);
    std::cout << "test_identity_1d passed" << std::endl;
}

void test_identity_3d() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 2;
    d3.data = {
        0,0,0, 1,0,0,
        0,1,0, 1,1,0,
        0,0,1, 1,0,1,
        0,1,1, 1,1,1
    };
    lut.grid3D = std::move(d3);
    
    std::array<float, 3> pixel = {0.25f, 0.5f, 0.75f};
    auto result = cubelut::Processor::process(lut, pixel);
    
    assert(std::abs(result[0] - 0.25f) < 1e-6);
    assert(std::abs(result[1] - 0.5f) < 1e-6);
    assert(std::abs(result[2] - 0.75f) < 1e-6);
    std::cout << "test_identity_3d passed" << std::endl;
}

void test_process_image() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 2;
    d3.data = {
        0,0,0, 1,0,0,
        0,1,0, 1,1,0,
        0,0,1, 1,0,1,
        0,1,1, 1,1,1
    };
    lut.grid3D = std::move(d3);

    std::vector<float> imageData = {
        0.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 1.0f,
        0.5f, 0.5f, 0.5f
    };

    cubelut::Processor::processImage(lut, imageData.data(), 3, 1);

    assert(std::abs(imageData[0] - 0.0f) < 1e-6);
    assert(std::abs(imageData[3] - 1.0f) < 1e-6);
    assert(std::abs(imageData[6] - 0.5f) < 1e-6);
    std::cout << "test_process_image passed" << std::endl;
}

void test_trilinear_math() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 2;
    // 构造一个具有明确线性规律的 LUT：
    // R_out = R_in * 10
    // G_out = G_in * 20
    // B_out = B_in * 30
    d3.data = {
        // Z=0 (B=0)
        0,0,0,    10,0,0,   // Y=0(G=0): X=0, X=1
        0,20,0,   10,20,0,  // Y=1(G=1): X=0, X=1
        // Z=1 (B=1)
        0,0,30,   10,0,30,  // Y=0(G=0): X=0, X=1
        0,20,30,  10,20,30  // Y=1(G=1): X=0, X=1
    };
    lut.grid3D = std::move(d3);
    
    // 输入一个带小数的 RGB: (0.1, 0.5, 0.9)
    // 理论预期结果应该是: (0.1*10, 0.5*20, 0.9*30) = (1.0, 10.0, 27.0)
    std::array<float, 3> pixel = {0.1f, 0.5f, 0.9f};
    auto result = cubelut::Processor::process(lut, pixel);
    
    assert(std::abs(result[0] - 1.0f) < 1e-5);
    assert(std::abs(result[1] - 10.0f) < 1e-5);
    assert(std::abs(result[2] - 27.0f) < 1e-5);
    std::cout << "test_trilinear_math passed" << std::endl;
}

void test_tetrahedral_math() {
    cubelut::Lut lut;
    cubelut::LutData3D d3;
    d3.size = 2;
    d3.data = {
        0,0,0,    10,0,0,   // Z=0, Y=0
        0,20,0,   10,20,0,  // Z=0, Y=1
        0,0,30,   10,0,30,  // Z=1, Y=0
        0,20,30,  10,20,30  // Z=1, Y=1
    };
    lut.grid3D = std::move(d3);
    
    // 我们构造一个 R > B > G 的点: (0.9, 0.1, 0.5)
    // 理论上它落入的四面体顶点为:
    // C000 = (0,0,0)
    // Ca (最大分量 R 进一位) = (10,0,0)
    // Cb (除了最小分量 G 外都进一位, 即 R和B 进一位) = (10,0,30)
    // C111 = (10,20,30)
    //
    // x0 = max = 0.9 (R)
    // x2 = min = 0.1 (G)
    // x1 = mid = 0.5 (B)
    //
    // Result = C000 + x0*(Ca - C000) + x1*(Cb - Ca) + x2*(C111 - Cb)
    //        = (0,0,0) + 0.9*(10,0,0) + 0.5*(0,0,30) + 0.1*(0,20,0)
    //        = (9, 2, 15)
    std::array<float, 3> pixel = {0.9f, 0.1f, 0.5f};
    auto result = cubelut::Processor::process(lut, pixel, cubelut::Interpolation::Tetrahedral);
    
    assert(std::abs(result[0] - 9.0f) < 1e-5);
    assert(std::abs(result[1] - 2.0f) < 1e-5);
    assert(std::abs(result[2] - 15.0f) < 1e-5);
    std::cout << "test_tetrahedral_math passed" << std::endl;
}

int main() {
    test_identity_1d();
    test_identity_3d();
    test_trilinear_math();
    test_tetrahedral_math();
    test_process_image();
    return 0;
}
