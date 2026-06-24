#pragma once

#include "core/render_features.h"
#include "core/render_view_mode.h"
#include "core/shadow_math.h"
#include "core/sponza_loader.h"
#include "core/types.h"
#include "render/material_textures.h"
#include "render/pipelines.h"
#include "render/render_resources.h"
#include "gpu/gpu.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct ForwardCascadeUniforms {
    float splitFar;
    float texelSize;
    float pad[2];
};

struct ForwardFeatureFlagsCpu {
    uint32_t enableDirLight;
    uint32_t enableDirShadow;
    uint32_t enablePointLights;
    uint32_t enableSSGI;
    uint32_t enableFog;
    uint32_t pointLightCount;
};

// Must match ForwardLighting in forward.slang.
struct ForwardLightingCpu {
    float dirLightDir[3];
    float dirLightIntensity;
    float dirLightColor[3];
    float pad0;
    float ambientColor[3];
    float pad1;
};

struct FrameData {
    GpuDevice device = nullptr;
    SponzaScene* scene = nullptr;
    DemoPipelines* pipelines = nullptr;
    RenderResources* resources = nullptr;
    MaterialTextures* materials = nullptr;

    CameraParams cameraParams = {};
    CascadeShadowData cascadeShadows[kCascadeCount] = {};
    float pointShadowViewProj[kMaxPointShadowSlots][6][16] = {};
    float pointShadowNear[kMaxPointShadowSlots] = {};
    float pointShadowFar[kMaxPointShadowSlots] = {};

    uint32_t lightCount = 0;
    float dirLightDir[3] = {};
    float dirLightColor[3] = {};
    float dirLightIntensity = 0.0f;
    float ambientColor[3] = {};

    uint32_t surfaceWidth = 1280;
    uint32_t surfaceHeight = 720;
    uint32_t frameIndex = 0;

    RenderFeatures features = {};
    RenderViewMode viewMode = RenderViewMode::Final;

    rhi::ComPtr<rhi::IBuffer> rhiVertexBuffer;
    rhi::ComPtr<rhi::IBuffer> rhiIndexBuffer;
};

enum class ShadowPassType { Cascade = 0, PointCubeFace = 1 };

struct ShadowPassData {
    FrameData* frame = nullptr;
    ShadowPassType type = ShadowPassType::Cascade;
    int cascadeIndex = 0;
    int pointShadowSlot = 0;
    int cubeFace = 0;
};

struct FrameGraphContext {
    GpuDevice device = nullptr;
    GpuSurfaceTexture backbuffer = nullptr;
    RenderResources* resources = nullptr;
    RenderFeatures features = {};
    FrameData* frame = nullptr;
};

void updatePointLights(FrameData& frame, const Vec3& sceneCenter, float timeSec);
