#pragma once

#include "render/frame_data.h"
#include "render/pipelines.h"
#include "core/camera.h"
#include "core/render_view_mode.h"
#include "gpu/gpu.h"
#include "gpu/platform/gpu_surface.h"

struct ShadowDepthStats {
    float minVal = 0.0f;
    float maxVal = 0.0f;
    uint32_t pixelsNearClear = 0;
    uint32_t pixelsNearZero = 0;
    uint32_t pixelsMidRange = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool readbackOk = false;
};

struct ColorBufferStats {
    float minLuma = 0.0f;
    float maxLuma = 0.0f;
    float meanLuma = 0.0f;
    uint32_t pixelsNearWhite = 0;
    uint32_t pixelsNearBlack = 0;
    uint32_t pixelsMid = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool readbackOk = false;
};

bool shadowDiagVerifyShaderLayout(DemoPipelines& pipelines);
bool shadowDiagReadbackCascadeDepth(GpuDevice device, GpuTextureHandle tex, ShadowDepthStats& stats);
bool shadowDiagReadbackSurface(GpuDevice device, GpuSurfaceTexture surf, uint32_t w, uint32_t h, ColorBufferStats& stats);
bool shadowDiagCheckViewMode(const ColorBufferStats& stats, RenderViewMode mode, char* failMsg, size_t failMsgSize);
bool shadowDiagCheckViewModeSimple(const ColorBufferStats& stats, RenderViewMode mode, char* failMsg, size_t failMsgSize);
void shadowDiagPrintColorStats(const ColorBufferStats& stats, const char* label);
void shadowDiagPrintBindingState(const FrameData& frame, DemoPipelines& pipelines);
void shadowDiagPrintStats(const ShadowDepthStats& stats, int cascadeIndex);
void shadowDiagPrintFrameCounters(const FrameData& frame);
void shadowDiagPrintCpuClip(const CascadeShadowData& cascade, const char* label, float x, float y, float z);

struct ShadowDumpContext {
    GpuDevice device = nullptr;
    uint32_t surfaceWidth = 0;
    uint32_t surfaceHeight = 0;
    FrameData* frame = nullptr;
    RenderResources* resources = nullptr;
    const char* outputDir = nullptr;
    const Vec3* boundsMin = nullptr;
    const Vec3* boundsMax = nullptr;
    const FlyCamera* camera = nullptr;
    // Render one view mode into an acquired swapchain image; return false on failure.
    bool (*renderView)(void* userData, RenderViewMode mode, GpuSurfaceTexture* outSurface) = nullptr;
    void (*releaseSurface)(void* userData, GpuSurfaceTexture surface) = nullptr;
    void* renderUserData = nullptr;
};

bool shadowDiagReadbackSurfaceToPpm(GpuDevice device, GpuSurfaceTexture surf, uint32_t w, uint32_t h,
                                    const char* path, ColorBufferStats* statsOut = nullptr);
bool shadowDiagReadbackCascadeDepthToPgm(GpuDevice device, GpuTextureHandle tex, const char* path,
                                           ShadowDepthStats* statsOut = nullptr);
bool shadowDiagDumpSession(const ShadowDumpContext& ctx);
