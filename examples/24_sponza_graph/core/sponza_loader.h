#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <cstdint>

struct SponzaVertex {
    float position[3];
    float normal[3];
    float uv[2];
    float material;
};

struct SponzaDraw {
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    uint32_t castsShadow = 1u;
};

struct SponzaMaterial {
    std::string name;
    std::string baseColor;
    std::string normal;
    uint32_t baseColorIndex = UINT32_MAX;
};

struct SponzaScene {
    std::vector<SponzaVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SponzaDraw> draws;
    std::vector<SponzaMaterial> materials;
    Vec3 boundsMin, boundsMax;
};

bool parseMtl(const char* path, SponzaScene& scene);
bool parseObj(const char* path, SponzaScene& scene);
