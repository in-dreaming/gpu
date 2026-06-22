#pragma once

#include "gpu/gpu.h"
#include "core/sponza_loader.h"
#include <cstdint>

struct MaterialTextures {
    GpuTextureHandle baseColorArray = GPU_NULL_HANDLE;
    GpuTextureHandle baseColorView = GPU_NULL_HANDLE;
    GpuTextureHandle normalArray = GPU_NULL_HANDLE;
    GpuTextureHandle normalView = GPU_NULL_HANDLE;
    uint32_t layerSize = 0;
    uint32_t layerCount = 0;
    uint32_t loadedBaseColorCount = 0;
};

bool createSponzaMaterialTextures(GpuDevice device, const char* root, SponzaScene& scene, MaterialTextures& out);
void destroySponzaMaterialTextures(GpuDevice device, MaterialTextures& tex);
