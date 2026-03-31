#pragma once

#include "vector3.h"
#include <vector>

// 前方宣言
enum class FluidType;

// SoA (Structure of Arrays) 形式のパーティクルデータコンテナ
struct ParticleSoA {
    std::vector<Vector3> positions;
    std::vector<Vector3> velocities;
    std::vector<Vector3> forces;
    std::vector<float> densities;
    std::vector<float> pressures;
    std::vector<float> temperatures;
    std::vector<FluidType> fluidTypes;

    size_t count = 0; // 現在のパーティクル数
    size_t capacity = 0; // 確保されている容量

    void resize(size_t newSize) {
        positions.resize(newSize);
        velocities.resize(newSize);
        forces.resize(newSize);
        densities.resize(newSize);
        pressures.resize(newSize);
        temperatures.resize(newSize);
        fluidTypes.resize(newSize);
        count = newSize;
        capacity = newSize;
    }

    void clear() {
        positions.clear();
        velocities.clear();
        forces.clear();
        densities.clear();
        pressures.clear();
        temperatures.clear();
        fluidTypes.clear();
        count = 0;
    }
};