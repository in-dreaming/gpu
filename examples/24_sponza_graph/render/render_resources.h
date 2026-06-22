#pragma once

#include "gpu/gpu.h"
#include "core/sponza_loader.h"
#include "core/types.h"

struct RenderResources {
    GpuDevice device;

    // Sponza geometry
    GpuBufferHandle vertexBuffer = GPU_NULL_HANDLE;
    GpuBufferHandle indexBuffer = GPU_NULL_HANDLE;

    // Depth buffer
    GpuTextureHandle sceneDepth = GPU_NULL_HANDLE;
    GpuTextureHandle sceneDepthView = GPU_NULL_HANDLE;

    // Light buffer
    GpuBufferHandle lightBuffer = GPU_NULL_HANDLE;

    // Per-frame camera data (uploaded as constant buffer)
    GpuBufferHandle cameraBuffer = GPU_NULL_HANDLE;

    // Light cull output
    GpuBufferHandle lightIndexBuffer = GPU_NULL_HANDLE;

    // SSGI textures
    GpuTextureHandle ssgiOutput = GPU_NULL_HANDLE;
    GpuTextureHandle ssgiOutputView = GPU_NULL_HANDLE;
    GpuTextureHandle ssgiPrev = GPU_NULL_HANDLE;
    GpuTextureHandle ssgiPrevView = GPU_NULL_HANDLE;

    // CSM shadow textures (4 cascades)
    GpuTextureHandle cascadeDepth[4] = {};
    GpuTextureHandle cascadeDepthView[4] = {};
    GpuTextureHandle cascadeSRV[4] = {};

    // Shared sampler
    GpuSamplerHandle linearSampler = GPU_NULL_HANDLE;
    GpuSamplerHandle shadowSampler = GPU_NULL_HANDLE;

    uint32_t surfaceWidth = 1280;
    uint32_t surfaceHeight = 720;
};

bool initRenderResources(RenderResources& r, uint32_t w, uint32_t h, uint32_t maxLights);
void recreateDepth(RenderResources& r, uint32_t w, uint32_t h);
void destroyRenderResources(RenderResources& r);
