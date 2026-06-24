#pragma once

#include "gpu/gpu.h"
#include "core/sponza_loader.h"
#include "core/types.h"
#include "render/material_textures.h"

#include <cstdint>

struct BindlessBindings {
    uint32_t baseColorTexture = UINT32_MAX;
    uint32_t linearSampler = UINT32_MAX;
    uint32_t shadowSampler = UINT32_MAX;
    uint32_t lightBuffer = UINT32_MAX;
    uint32_t lightIndexBuffer = UINT32_MAX;
    uint32_t cascadeShadows[4] = {};
    uint32_t ssgiTexture = UINT32_MAX;
    uint32_t sceneDepth = UINT32_MAX;
    uint32_t sceneAlbedo = UINT32_MAX;
    uint32_t sceneNormal = UINT32_MAX;
    uint32_t ssgiOutputUav = UINT32_MAX;
};

struct RenderResources {
    GpuDevice device;

    // Sponza geometry
    GpuBufferHandle vertexBuffer = GPU_NULL_HANDLE;
    GpuBufferHandle indexBuffer = GPU_NULL_HANDLE;

    // Scene depth + G-buffer
    GpuTextureHandle sceneDepth = GPU_NULL_HANDLE;
    GpuTextureHandle sceneDepthDsv = GPU_NULL_HANDLE;
    GpuTextureHandle sceneDepthSrv = GPU_NULL_HANDLE;
    GpuTextureHandle sceneAlbedo = GPU_NULL_HANDLE;
    GpuTextureHandle sceneAlbedoRtv = GPU_NULL_HANDLE;
    GpuTextureHandle sceneAlbedoSrv = GPU_NULL_HANDLE;
    GpuTextureHandle sceneNormal = GPU_NULL_HANDLE;
    GpuTextureHandle sceneNormalRtv = GPU_NULL_HANDLE;
    GpuTextureHandle sceneNormalSrv = GPU_NULL_HANDLE;

    // Light buffer
    GpuBufferHandle lightBuffer = GPU_NULL_HANDLE;

    // Per-frame camera data (uploaded as constant buffer)
    GpuBufferHandle cameraBuffer = GPU_NULL_HANDLE;

    // Light cull output
    GpuBufferHandle lightIndexBuffer = GPU_NULL_HANDLE;

    // SSGI textures
    GpuTextureHandle ssgiOutput = GPU_NULL_HANDLE;
    GpuTextureHandle ssgiOutputUav = GPU_NULL_HANDLE;
    GpuTextureHandle ssgiOutputSrv = GPU_NULL_HANDLE;
    GpuTextureHandle ssgiPrev = GPU_NULL_HANDLE;
    GpuTextureHandle ssgiPrevView = GPU_NULL_HANDLE;

    // CSM shadow textures (4 cascades)
    GpuTextureHandle cascadeDepth[4] = {};
    GpuTextureHandle cascadeDepthView[4] = {};
    GpuTextureHandle cascadeSRV[4] = {};

    // Point-light omnidirectional cube shadows (6 faces per slot)
    GpuTextureHandle pointShadowCube[kMaxPointShadowSlots] = {};
    GpuTextureHandle pointShadowCubeSRV[kMaxPointShadowSlots] = {};
    GpuTextureHandle pointShadowCubeFaceDSV[kMaxPointShadowSlots][6] = {};

    // Shared samplers
    GpuSamplerHandle linearSampler = GPU_NULL_HANDLE;
    GpuSamplerHandle shadowSampler = GPU_NULL_HANDLE;
    GpuSamplerHandle pointShadowDepthSampler = GPU_NULL_HANDLE;

    GpuBindlessHeap textureBindlessHeap = nullptr;
    GpuBindlessHeap samplerBindlessHeap = nullptr;
    GpuBindlessHeap bufferBindlessHeap = nullptr;
    BindlessBindings bindless = {};

    uint32_t surfaceWidth = 1280;
    uint32_t surfaceHeight = 720;
};

bool initRenderResources(RenderResources& r, uint32_t w, uint32_t h, uint32_t maxLights);
bool registerBindlessResources(RenderResources& r, const MaterialTextures& materials);
bool validateBindlessBindings(RenderResources& r);
bool refreshSceneDepthBindlessHandle(RenderResources& r);
bool refreshGBufferBindlessHandles(RenderResources& r);
void recreateGBuffer(RenderResources& r, uint32_t w, uint32_t h);
void recreateDepth(RenderResources& r, uint32_t w, uint32_t h);
void destroyRenderResources(RenderResources& r);
