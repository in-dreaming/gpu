#include "render/shadow_diag.h"
#include "render/pipelines.h"
#include "core/render_view_mode.h"

#include "gpu/core/gpu_internal.h"
#include "gpu/resource/gpu_barrier.h"
#include "gpu/resource/gpu_readback.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

using namespace rhi;

static float readDepthPixel(const uint8_t* base, uint32_t rowPitch, uint32_t x, uint32_t y)
{
    const float* row = reinterpret_cast<const float*>(base + (size_t)y * rowPitch);
    return row[x];
}

bool shadowDiagVerifyShaderLayout(DemoPipelines& pipelines)
{
    bool ok = true;
    if (!pipelines.forwardRootObj) {
        printf("[diag] forward root object missing\n");
        return false;
    }
    if (!pipelines.shadowRootObj) {
        printf("[diag] shadow root object missing\n");
        return false;
    }

    ShaderCursor fwd(pipelines.forwardRootObj.get());
    ShaderCursor sh(pipelines.shadowRootObj.get());

    const char* fwdFields[] = {"gFwd", "cascadeViewProjs", "pointShadowViewProjs", "gCascadeSplitFar", "gCascadeTexelSize",
                               "gCascadeWorldTexel", "gCascadeDepthBias", "gPointShadowNearFar", "enableDirShadowFlag",
                               "enableDirLightFlag", "enablePointShadowFlag", "shadowMap0", "shadowMap1",
                               "shadowMap2", "shadowMap3", "shadowSampler", "pointShadowCube0", "pointShadowCube7",
                               "pointShadowDepthSampler", "baseColorArray"};
    for (const char* name : fwdFields) {
        bool valid = fwd[name].isValid();
        printf("[diag] forward field '%s' valid=%d\n", name, valid ? 1 : 0);
        ok = ok && valid;
    }

    const char* shFields[] = {"cascadeViewProjs", "shadowCascadeIndex", "shadowViewProj"};
    for (const char* name : shFields) {
        bool valid = sh[name].isValid();
        printf("[diag] shadow field '%s' valid=%d\n", name, valid ? 1 : 0);
        ok = ok && valid;
    }

    return ok;
}

void shadowDiagPrintBindingState(const FrameData& frame, DemoPipelines& pipelines)
{
    if (!frame.device || !frame.resources || !pipelines.forwardRootObj) return;
    const RenderResources& res = *frame.resources;

    auto resolveView = [&](GpuTextureHandle h) -> ITextureView* {
        if (!gpuHandleIsValid(h)) return nullptr;
        return frame.device->textureViewPool.resolve(h.index, h.generation);
    };

    printf("[diag] cascade DSV/SRV handles:\n");
    for (int i = 0; i < 4; i++) {
        auto* dsv = resolveView(res.cascadeDepthView[i]);
        auto* srv = resolveView(res.cascadeSRV[i]);
        printf("  cascade[%d] tex=(%u,%u) dsv=(%u,%u) srv=(%u,%u) dsvPtr=%p srvPtr=%p\n",
               i,
               res.cascadeDepth[i].index, res.cascadeDepth[i].generation,
               res.cascadeDepthView[i].index, res.cascadeDepthView[i].generation,
               res.cascadeSRV[i].index, res.cascadeSRV[i].generation,
               (void*)dsv, (void*)srv);
    }
}

static bool readbackDepthTextureRaw(GpuDevice device, GpuTextureHandle tex, std::vector<uint8_t>& outBytes,
                                    uint32_t& outWidth, uint32_t& outHeight, uint32_t& outRowPitch)
{
    outBytes.clear();
    outWidth = outHeight = outRowPitch = 0;
    if (!device || !gpuHandleIsValid(tex)) return false;

    rhi::ITexture* rhiTex = device->texturePool.resolve(tex.index, tex.generation);
    if (!rhiTex) return false;

    const auto& desc = rhiTex->getDesc();
    outWidth = desc.size.width;
    outHeight = desc.size.height;

    rhi::SubresourceLayout layout = {};
    if (SLANG_FAILED(rhiTex->getSubresourceLayout(0, &layout))) {
        outRowPitch = gpuGetReadbackRowPitch(tex, device);
    } else {
        outRowPitch = (uint32_t)layout.rowPitch;
    }
    if (outRowPitch == 0) return false;

    const uint64_t bufSize = (uint64_t)outRowPitch * outHeight;
    GpuBufferHandle readback = {};
    if (gpuCreateReadbackBuffer(device, bufSize, &readback) != GPU_SUCCESS) return false;

    GpuCommandQueue queue = nullptr;
    if (gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue) != GPU_SUCCESS || !queue) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(readback.index, readback.generation);
    if (!rhiBuf) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
    if (!encoder) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    encoder->rhiEncoder->setTextureState(rhiTex, rhi::kEntireTexture, rhi::ResourceState::CopySource);
    encoder->rhiEncoder->setBufferState(rhiBuf, rhi::ResourceState::CopyDestination);
    encoder->rhiEncoder->globalBarrier();
    encoder->rhiEncoder->copyTextureToBuffer(
        rhiBuf, 0, bufSize, outRowPitch, rhiTex, 0, 0, rhi::Offset3D{0, 0, 0},
        rhi::Extent3D{outWidth, outHeight, 1});
    encoder->rhiEncoder->setTextureState(rhiTex, rhi::kEntireTexture, rhi::ResourceState::ShaderResource);
    encoder->rhiEncoder->globalBarrier();

    GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
    if (!cmd) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    gpuQueueSubmit(queue, 1, &cmd);
    gpuQueueWaitOnHost(queue);

    void* mapped = nullptr;
    if (gpuMapReadbackBuffer(device, readback, &mapped) != GPU_SUCCESS || !mapped) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    outBytes.resize((size_t)bufSize);
    memcpy(outBytes.data(), mapped, (size_t)bufSize);
    gpuUnmapReadbackBuffer(device, readback);
    gpuDestroyBuffer(device, readback);
    return true;
}

static void computeDepthStats(const uint8_t* bytes, uint32_t rowPitch, uint32_t w, uint32_t h, ShadowDepthStats& stats)
{
    stats = {};
    stats.width = w;
    stats.height = h;
    stats.minVal = 1e9f;
    stats.maxVal = -1e9f;
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            float d = readDepthPixel(bytes, rowPitch, x, y);
            stats.minVal = std::min(stats.minVal, d);
            stats.maxVal = std::max(stats.maxVal, d);
            if (d > 0.999f) stats.pixelsNearClear++;
            else if (d < 0.001f) stats.pixelsNearZero++;
            else stats.pixelsMidRange++;
        }
    }
    stats.readbackOk = true;
}

bool shadowDiagReadbackCascadeDepth(GpuDevice device, GpuTextureHandle tex, ShadowDepthStats& stats)
{
    stats = {};
    std::vector<uint8_t> bytes;
    uint32_t w = 0, h = 0, rowPitch = 0;
    if (!readbackDepthTextureRaw(device, tex, bytes, w, h, rowPitch)) {
        printf("[diag] readback: depth copy failed\n");
        return false;
    }
    computeDepthStats(bytes.data(), rowPitch, w, h, stats);
    return true;
}

void shadowDiagPrintStats(const ShadowDepthStats& stats, int cascadeIndex)
{
    if (cascadeIndex < 0)
        printf("[diag] scene depth readback %s (%ux%u)\n", stats.readbackOk ? "OK" : "FAILED", stats.width, stats.height);
    else
        printf("[diag] cascade[%d] depth readback %s (%ux%u)\n",
               cascadeIndex,
               stats.readbackOk ? "OK" : "FAILED",
               stats.width,
               stats.height);
    if (!stats.readbackOk) return;
    printf("[diag]   min=%.6f max=%.6f\n", stats.minVal, stats.maxVal);
    printf("[diag]   nearClear(>0.999)=%u nearZero(<0.001)=%u midRange=%u total=%u\n",
           stats.pixelsNearClear,
           stats.pixelsNearZero,
           stats.pixelsMidRange,
           stats.width * stats.height);
}

void shadowDiagPrintFrameCounters(const FrameData& frame)
{
    printf("[diag] shadow passes executed=%u shadow draw calls=%u forward uniforms ok=%d\n",
           frame.diagShadowPasses,
           frame.diagShadowDraws,
           frame.diagForwardUniformOk ? 1 : 0);
}

void shadowDiagPrintCpuClip(const CascadeShadowData& cascade, const char* label, float x, float y, float z)
{
    const float* m = cascade.viewProj;
    float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
    float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
    float cz = m[2] * x + m[6] * y + m[10] * z + m[14];
    float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
    float iw = 1.0f / fmaxf(fabsf(cw), 1e-5f);
    printf("[diag] cpu clip %s: clip=(%.4f,%.4f,%.4f,%.4f) ndc=(%.4f,%.4f,%.4f)\n",
           label, cx, cy, cz, cw, cx * iw, cy * iw, cz * iw);
}

static float pixelLuma(uint32_t bgra)
{
    float b = (float)((bgra >> 0) & 0xFF) / 255.0f;
    float g = (float)((bgra >> 8) & 0xFF) / 255.0f;
    float r = (float)((bgra >> 16) & 0xFF) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

static void computeColorStats(const uint8_t* bytes, uint32_t rowPitch, uint32_t w, uint32_t h, ColorBufferStats& stats);

bool shadowDiagReadbackSurface(GpuDevice device, GpuSurfaceTexture surf, uint32_t w, uint32_t h, ColorBufferStats& stats)
{
    stats = {};
    if (!device || !surf || !surf->rhiTexture || w == 0 || h == 0) return false;

    stats.width = w;
    stats.height = h;

    const uint32_t bpp = 4;
    const uint32_t rowPitch = (w * bpp + 255u) & ~255u;
    const uint64_t bufSize = (uint64_t)rowPitch * h;

    GpuBufferHandle readback = {};
    if (gpuCreateReadbackBuffer(device, bufSize, &readback) != GPU_SUCCESS) return false;

    GpuCommandQueue queue = nullptr;
    if (gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue) != GPU_SUCCESS || !queue) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    rhi::ITexture* rhiTex = surf->rhiTexture.get();
    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(readback.index, readback.generation);
    if (!rhiTex || !rhiBuf) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
    if (!encoder) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    encoder->rhiEncoder->setTextureState(rhiTex, rhi::kEntireTexture, rhi::ResourceState::CopySource);
    encoder->rhiEncoder->setBufferState(rhiBuf, rhi::ResourceState::CopyDestination);
    encoder->rhiEncoder->globalBarrier();
    encoder->rhiEncoder->copyTextureToBuffer(
        rhiBuf, 0, bufSize, rowPitch, rhiTex, 0, 0, rhi::Offset3D{0, 0, 0}, rhi::Extent3D{w, h, 1});

    GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
    if (!cmd) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    gpuQueueSubmit(queue, 1, &cmd);
    gpuQueueWaitOnHost(queue);

    void* mapped = nullptr;
    if (gpuMapReadbackBuffer(device, readback, &mapped) != GPU_SUCCESS || !mapped) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(mapped);
    std::vector<uint8_t> copy((size_t)bufSize);
    memcpy(copy.data(), bytes, (size_t)bufSize);
    computeColorStats(copy.data(), rowPitch, w, h, stats);

    gpuUnmapReadbackBuffer(device, readback);
    gpuDestroyBuffer(device, readback);
    return true;
}

void shadowDiagPrintColorStats(const ColorBufferStats& stats, const char* label)
{
    printf("[verify] %s readback %s (%ux%u)\n", label, stats.readbackOk ? "OK" : "FAILED", stats.width, stats.height);
    if (!stats.readbackOk) return;
    printf("[verify]   luma min=%.4f max=%.4f mean=%.4f\n", stats.minLuma, stats.maxLuma, stats.meanLuma);
    if (stats.centerPixelCount > 0) {
        printf("[verify]   center luma min=%.4f max=%.4f mean=%.4f nearBlack=%u/%u\n",
               stats.centerMinLuma, stats.centerMaxLuma, stats.centerMeanLuma, stats.centerPixelsNearBlack,
               stats.centerPixelCount);
    }
    printf("[verify]   nearWhite(>0.95)=%u nearBlack(<0.05)=%u mid=%u total=%u\n",
           stats.pixelsNearWhite, stats.pixelsNearBlack, stats.pixelsMid, stats.width * stats.height);
}

bool shadowDiagCheckViewMode(const ColorBufferStats& stats, RenderViewMode mode, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }

    const uint32_t total = stats.width * stats.height;
    if (total == 0) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "empty image");
        return false;
    }

    const float blackRatio = (float)stats.pixelsNearBlack / (float)total;
    const float lumaSpan = stats.maxLuma - stats.minLuma;

    switch (mode) {
    case RenderViewMode::Shadow:
        if (stats.minLuma > 0.85f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "shadow view min luma %.3f too bright", stats.minLuma);
            return false;
        }
        if (blackRatio > 0.995f) {
            if (failMsg && failMsgSize > 0)
                snprintf(failMsg, failMsgSize, "shadow view %.1f%% nearBlack (compare broken or shadows off)", blackRatio * 100.0f);
            return false;
        }
        if (stats.maxLuma < 0.12f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "shadow view max luma %.3f too dark", stats.maxLuma);
            return false;
        }
        if (lumaSpan < 0.05f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "shadow view luma span %.3f too small", lumaSpan);
            return false;
        }
        return true;

    case RenderViewMode::ShadowMap:
        if (blackRatio > 0.98f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "shadowmap view >98%% black");
            return false;
        }
        if (lumaSpan < 0.05f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "shadowmap view luma span %.3f too small", lumaSpan);
            return false;
        }
        return true;

    case RenderViewMode::ShadowAtlas:
        if (lumaSpan < 0.05f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "shadowatlas depth span %.3f too small", lumaSpan);
            return false;
        }
        if (stats.maxLuma < 0.01f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "shadowatlas max depth %.3f near zero", stats.maxLuma);
            return false;
        }
        return true;

    case RenderViewMode::Final:
        if (lumaSpan < 0.15f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "final view luma span %.3f too small", lumaSpan);
            return false;
        }
        return true;

    default:
        return true;
    }
}

bool shadowDiagCheckViewModeSimple(const ColorBufferStats& stats, RenderViewMode mode, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }

    const uint32_t total = stats.width * stats.height;
    if (total == 0) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "empty image");
        return false;
    }

    const float blackRatio = (float)stats.pixelsNearBlack / (float)total;
    const float whiteRatio = (float)stats.pixelsNearWhite / (float)total;
    const float lumaSpan = stats.maxLuma - stats.minLuma;

    switch (mode) {
    case RenderViewMode::Shadow:
        if (stats.meanLuma < 0.12f || stats.meanLuma > 0.88f) {
            if (failMsg && failMsgSize > 0)
                snprintf(failMsg, failMsgSize, "simple shadow mean %.3f outside [0.12,0.88]", stats.meanLuma);
            return false;
        }
        if (blackRatio > 0.80f) {
            if (failMsg && failMsgSize > 0)
                snprintf(failMsg, failMsgSize, "simple shadow %.1f%% nearBlack", blackRatio * 100.0f);
            return false;
        }
        if (whiteRatio < 0.01f) {
            if (failMsg && failMsgSize > 0)
                snprintf(failMsg, failMsgSize, "simple shadow lit area too small (white=%.1f%%)", whiteRatio * 100.0f);
            return false;
        }
        if (lumaSpan < 0.25f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "simple shadow span %.3f too small", lumaSpan);
            return false;
        }
        return true;

    case RenderViewMode::ShadowMap:
    case RenderViewMode::ShadowAtlas:
        return shadowDiagCheckViewMode(stats, mode, failMsg, failMsgSize);

    case RenderViewMode::Final:
        if (lumaSpan < 0.10f) {
            if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "simple final span %.3f too small", lumaSpan);
            return false;
        }
        return true;

    default:
        return shadowDiagCheckViewMode(stats, mode, failMsg, failMsgSize);
    }
}

bool shadowDiagCheckPointLightsView(const ColorBufferStats& stats, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }

    const uint32_t total = stats.width * stats.height;
    if (total == 0) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "empty image");
        return false;
    }

    const float lumaSpan = stats.maxLuma - stats.minLuma;
    const float blackRatio = (float)stats.pixelsNearBlack / (float)total;

    if (stats.maxLuma < 0.04f) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "points view max luma %.3f too dark", stats.maxLuma);
        return false;
    }
    if (lumaSpan < 0.06f) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "points view span %.3f too small", lumaSpan);
        return false;
    }
    if (blackRatio < 0.002f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "points view lacks shadow darkening (nearBlack=%.2f%%)", blackRatio * 100.0f);
        return false;
    }
    return true;
}

bool shadowDiagCheckLightTestFinal(const ColorBufferStats& stats, uint32_t pointCount, bool pointShadows,
                                   char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }

    const uint32_t total = stats.width * stats.height;
    if (total == 0) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "empty image");
        return false;
    }

    const float lumaSpan = stats.maxLuma - stats.minLuma;
    const float blackRatio = (float)stats.pixelsNearBlack / (float)total;

    if (stats.maxLuma < 0.03f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "final too dark (max=%.3f) with %u point(s)", stats.maxLuma, pointCount);
        return false;
    }
    if (lumaSpan < 0.04f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "final span %.3f too small (%u point(s))", lumaSpan, pointCount);
        return false;
    }

    if (pointCount >= 2 && lumaSpan < 0.07f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "dual-light span %.3f too small", lumaSpan);
        return false;
    }

    if (pointShadows && blackRatio < 0.001f && stats.minLuma > stats.maxLuma * 0.55f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "point-shadow case lacks contrast (min=%.3f max=%.3f)",
                     stats.minLuma, stats.maxLuma);
        return false;
    }

    return true;
}

bool shadowDiagCheckDefaultLightTestPoints(const ColorBufferStats& stats, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }
    if (stats.maxLuma < 0.04f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "default points view max luma %.3f (expect lit areas)", stats.maxLuma);
        return false;
    }
    return true;
}

bool shadowDiagCheckDefaultLightTestShadow(const ColorBufferStats& stats, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }
    const uint32_t total = stats.width * stats.height;
    if (total == 0) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "empty image");
        return false;
    }
    const float span = stats.maxLuma - stats.minLuma;
    if (span < 0.08f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "shadow view span %.3f too small", span);
        return false;
    }
    const float blackRatio = (float)stats.pixelsNearBlack / (float)total;
    if (blackRatio < 0.05f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "shadow view lacks dark texels (%.1f%% nearBlack)", blackRatio * 100.0f);
        return false;
    }
    return true;
}

bool shadowDiagCheckDefaultLightTestFinal(const ColorBufferStats& stats, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }
    const uint32_t total = stats.width * stats.height;
    if (total == 0) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "empty image");
        return false;
    }
    const float span = stats.maxLuma - stats.minLuma;
    if (stats.maxLuma < 0.08f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "final too dark (max=%.3f)", stats.maxLuma);
        return false;
    }
    if (span < 0.10f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "final span %.3f too small (shadows not visible)", span);
        return false;
    }
    if (stats.minLuma > stats.maxLuma * 0.55f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "final lacks contrast (min=%.3f max=%.3f)", stats.minLuma, stats.maxLuma);
        return false;
    }
    return true;
}

bool shadowDiagCheckSsgiView(const ColorBufferStats& stats, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "readback failed");
        return false;
    }
    if (stats.centerPixelCount == 0) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "center region empty");
        return false;
    }
    const float span = stats.maxLuma - stats.minLuma;
    if (stats.maxLuma < 0.03f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "SSGI view too dark (max=%.3f)", stats.maxLuma);
        return false;
    }
    if (span < 0.02f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "SSGI view span %.3f too small", span);
        return false;
    }
    if (stats.centerMaxLuma < 0.08f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "center geometry too dark (centerMax=%.3f)", stats.centerMaxLuma);
        return false;
    }
    if (stats.centerMeanLuma < 0.04f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "center mean luma %.3f too low", stats.centerMeanLuma);
        return false;
    }
    const float centerSpan = stats.centerMaxLuma - stats.centerMinLuma;
    if (centerSpan < 0.06f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "center GI too flat (span=%.3f)", centerSpan);
        return false;
    }
    const float centerNearBlackRatio = (float)stats.centerPixelsNearBlack / (float)stats.centerPixelCount;
    if (centerNearBlackRatio > 0.70f) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "center %.0f%% near-black (expected geometry lit)", centerNearBlackRatio * 100.0f);
        return false;
    }
    return true;
}

bool shadowDiagCheckPointShadowDepth(GpuDevice device, GpuTextureHandle cubeTex, char* failMsg, size_t failMsgSize)
{
    if (failMsg && failMsgSize > 0) failMsg[0] = 0;
    ShadowDepthStats stats = {};
    if (!shadowDiagReadbackCascadeDepth(device, cubeTex, stats)) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "cube depth readback failed");
        return false;
    }
    shadowDiagPrintStats(stats, 100);
    if (!stats.readbackOk) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "cube depth stats unavailable");
        return false;
    }
    if (stats.maxVal < 0.02f) {
        if (failMsg && failMsgSize > 0) snprintf(failMsg, failMsgSize, "cube depth max %.4f (empty shadow pass)", stats.maxVal);
        return false;
    }
    if (stats.pixelsMidRange < 8 && stats.pixelsNearZero < 8) {
        if (failMsg && failMsgSize > 0)
            snprintf(failMsg, failMsgSize, "cube depth lacks occluders (mid=%u zero=%u)", stats.pixelsMidRange,
                     stats.pixelsNearZero);
        return false;
    }
    return true;
}

static bool readbackSurfaceRaw(GpuDevice device, GpuSurfaceTexture surf, uint32_t w, uint32_t h,
                               std::vector<uint8_t>& outBytes, uint32_t& outRowPitch)
{
    outBytes.clear();
    outRowPitch = 0;
    if (!device || !surf || !surf->rhiTexture || w == 0 || h == 0) return false;

    const uint32_t bpp = 4;
    outRowPitch = (w * bpp + 255u) & ~255u;
    const uint64_t bufSize = (uint64_t)outRowPitch * h;

    GpuBufferHandle readback = {};
    if (gpuCreateReadbackBuffer(device, bufSize, &readback) != GPU_SUCCESS) return false;

    GpuCommandQueue queue = nullptr;
    if (gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue) != GPU_SUCCESS || !queue) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    rhi::ITexture* rhiTex = surf->rhiTexture.get();
    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(readback.index, readback.generation);
    if (!rhiTex || !rhiBuf) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
    if (!encoder) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    encoder->rhiEncoder->setTextureState(rhiTex, rhi::kEntireTexture, rhi::ResourceState::CopySource);
    encoder->rhiEncoder->setBufferState(rhiBuf, rhi::ResourceState::CopyDestination);
    encoder->rhiEncoder->globalBarrier();
    encoder->rhiEncoder->copyTextureToBuffer(
        rhiBuf, 0, bufSize, outRowPitch, rhiTex, 0, 0, rhi::Offset3D{0, 0, 0}, rhi::Extent3D{w, h, 1});

    GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
    if (!cmd) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    gpuQueueSubmit(queue, 1, &cmd);
    gpuQueueWaitOnHost(queue);

    void* mapped = nullptr;
    if (gpuMapReadbackBuffer(device, readback, &mapped) != GPU_SUCCESS || !mapped) {
        gpuDestroyBuffer(device, readback);
        return false;
    }

    outBytes.resize((size_t)bufSize);
    memcpy(outBytes.data(), mapped, (size_t)bufSize);
    gpuUnmapReadbackBuffer(device, readback);
    gpuDestroyBuffer(device, readback);
    return true;
}

static void computeColorStats(const uint8_t* bytes, uint32_t rowPitch, uint32_t w, uint32_t h, ColorBufferStats& stats)
{
    stats = {};
    stats.width = w;
    stats.height = h;
    double sum = 0.0;
    stats.minLuma = 1e9f;
    stats.maxLuma = -1e9f;
    stats.centerMinLuma = 1e9f;
    stats.centerMaxLuma = -1e9f;
    const uint32_t x0 = w / 4u;
    const uint32_t x1 = w - x0;
    const uint32_t y0 = h / 4u;
    const uint32_t y1 = h - y0;
    double centerSum = 0.0;
    const uint32_t total = w * h;
    for (uint32_t y = 0; y < h; y++) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(bytes + (size_t)y * rowPitch);
        for (uint32_t x = 0; x < w; x++) {
            float l = pixelLuma(row[x]);
            stats.minLuma = std::min(stats.minLuma, l);
            stats.maxLuma = std::max(stats.maxLuma, l);
            sum += l;
            if (l > 0.95f) stats.pixelsNearWhite++;
            else if (l < 0.05f) stats.pixelsNearBlack++;
            else stats.pixelsMid++;

            if (x >= x0 && x < x1 && y >= y0 && y < y1) {
                stats.centerMinLuma = std::min(stats.centerMinLuma, l);
                stats.centerMaxLuma = std::max(stats.centerMaxLuma, l);
                centerSum += l;
                stats.centerPixelCount++;
                if (l < 0.05f) stats.centerPixelsNearBlack++;
            }
        }
    }
    stats.meanLuma = total ? (float)(sum / total) : 0.0f;
    stats.centerMeanLuma = stats.centerPixelCount ? (float)(centerSum / stats.centerPixelCount) : 0.0f;
    stats.readbackOk = true;
}

static bool writePpmRgb(const char* path, const uint8_t* bgraBytes, uint32_t rowPitch, uint32_t w, uint32_t h)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    out << "P6\n" << w << " " << h << "\n255\n";
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t* row = bgraBytes + (size_t)y * rowPitch;
        for (uint32_t x = 0; x < w; x++) {
            const uint8_t* px = row + x * 4;
            out.put((char)px[2]);
            out.put((char)px[1]);
            out.put((char)px[0]);
        }
    }
    return out.good();
}

static bool writeRawDepthBin(const char* path, const uint8_t* depthBytes, uint32_t rowPitch, uint32_t w, uint32_t h)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const uint32_t hdr[2] = {w, h};
    out.write(reinterpret_cast<const char*>(hdr), sizeof(hdr));
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            float d = readDepthPixel(depthBytes, rowPitch, x, y);
            out.write(reinterpret_cast<const char*>(&d), sizeof(d));
        }
    }
    return out.good();
}

static bool readbackCascadeDepthRaw(GpuDevice device, GpuTextureHandle tex, std::vector<uint8_t>& bytes,
                                    uint32_t& w, uint32_t& h, uint32_t& rowPitch, ShadowDepthStats* statsOut)
{
    if (!readbackDepthTextureRaw(device, tex, bytes, w, h, rowPitch)) return false;
    if (statsOut) computeDepthStats(bytes.data(), rowPitch, w, h, *statsOut);
    return true;
}

static bool writeDepthCompareCsv(const char* path, const char* shadowMapPpm, const char* shadowUvPpm,
                                 const char* rawDepthBin, int cascadeIndex)
{
    auto readPpmRgb = [](const char* ppmPath, uint32_t& w, uint32_t& h, std::vector<float>& rgb) -> bool {
        std::ifstream in(ppmPath, std::ios::binary);
        if (!in) return false;
        std::string magic;
        in >> magic;
        if (magic != "P6") return false;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            break;
        }
        std::istringstream iss(line);
        uint32_t pw = 0, ph = 0;
        if (!(iss >> pw >> ph)) return false;
        int maxval = 0;
        in >> maxval;
        in.get();
        rgb.resize((size_t)pw * ph * 3);
        in.read(reinterpret_cast<char*>(rgb.data()), rgb.size());
        if (!in) return false;
        w = pw;
        h = ph;
        for (float& c : rgb) c /= 255.0f;
        return true;
    };

    uint32_t sw = 0, sh = 0;
    std::vector<float> sm, suv;
    if (!readPpmRgb(shadowMapPpm, sw, sh, sm)) return false;
    if (!readPpmRgb(shadowUvPpm, sw, sh, suv)) return false;

    std::ifstream raw(rawDepthBin, std::ios::binary);
    if (!raw) return false;
    uint32_t cw = 0, ch = 0;
    raw.read(reinterpret_cast<char*>(&cw), 4);
    raw.read(reinterpret_cast<char*>(&ch), 4);
    if (!raw || cw == 0 || ch == 0) return false;
    std::vector<float> cpu((size_t)cw * ch);
    raw.read(reinterpret_cast<char*>(cpu.data()), cpu.size() * sizeof(float));
    if (!raw) return false;

    std::ofstream out(path);
    if (!out) return false;
    out << "x,y,uv_u,uv_v,shader_load,cpu_readback,ndc_z,shader_cpu,shader_ndc,cpu_ndc\n";
    const int step = 16;
    for (uint32_t y = 0; y < sh; y += (uint32_t)step) {
        for (uint32_t x = 0; x < sw; x += (uint32_t)step) {
            const size_t i = (size_t)y * sw + x;
            const float u = suv[i * 3 + 0];
            const float v = suv[i * 3 + 1];
            const float ndcZ = suv[i * 3 + 2];
            const float shaderLoad = sm[i * 3 + 0];
            if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f || ndcZ < 0.01f || shaderLoad < 0.01f) continue;
            const uint32_t tx = std::min<uint32_t>((uint32_t)(u * (float)cw + 0.5f), cw - 1);
            const uint32_t ty = std::min<uint32_t>((uint32_t)(v * (float)ch + 0.5f), ch - 1);
            const float cpuDepth = cpu[(size_t)ty * cw + tx];
            if (cpuDepth < 0.01f) continue;
            out << x << "," << y << "," << u << "," << v << "," << shaderLoad << "," << cpuDepth << "," << ndcZ
                << "," << (shaderLoad / cpuDepth) << "," << (shaderLoad / ndcZ) << "," << (cpuDepth / ndcZ) << "\n";
        }
    }
    (void)cascadeIndex;
    return true;
}

static bool writePgm16Gray(const char* path, const uint8_t* depthBytes, uint32_t rowPitch, uint32_t w, uint32_t h,
                           float minVal, float maxVal)
{
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    const float span = std::max(maxVal - minVal, 1e-6f);
    out << "P5\n" << w << " " << h << "\n65535\n";
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            float d = readDepthPixel(depthBytes, rowPitch, x, y);
            float norm = (d - minVal) / span;
            norm = std::clamp(norm, 0.0f, 1.0f);
            uint16_t v = (uint16_t)(norm * 65535.0f + 0.5f);
            out.put((char)(v >> 8));
            out.put((char)(v & 0xFF));
        }
    }
    return out.good();
}

bool shadowDiagReadbackSurfaceToPpm(GpuDevice device, GpuSurfaceTexture surf, uint32_t w, uint32_t h,
                                    const char* path, ColorBufferStats* statsOut)
{
    std::vector<uint8_t> bytes;
    uint32_t rowPitch = 0;
    if (!readbackSurfaceRaw(device, surf, w, h, bytes, rowPitch)) return false;
    if (statsOut) computeColorStats(bytes.data(), rowPitch, w, h, *statsOut);
    return writePpmRgb(path, bytes.data(), rowPitch, w, h);
}

bool shadowDiagReadbackCascadeDepthToPgm(GpuDevice device, GpuTextureHandle tex, const char* path,
                                           ShadowDepthStats* statsOut)
{
    std::vector<uint8_t> bytes;
    uint32_t w = 0, h = 0, rowPitch = 0;
    if (!readbackDepthTextureRaw(device, tex, bytes, w, h, rowPitch)) return false;
    ShadowDepthStats stats = {};
    computeDepthStats(bytes.data(), rowPitch, w, h, stats);
    if (statsOut) *statsOut = stats;
    return writePgm16Gray(path, bytes.data(), rowPitch, w, h, stats.minVal, stats.maxVal);
}

static void reportCpuClip(std::ofstream& report, const CascadeShadowData& cascade, const char* label, float x, float y,
                          float z)
{
    const float* m = cascade.viewProj;
    float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
    float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
    float cz = m[2] * x + m[6] * y + m[10] * z + m[14];
    float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
    float iw = 1.0f / fmaxf(fabsf(cw), 1e-5f);
    report << "cpu clip " << label << ": clip=(" << cx << "," << cy << "," << cz << "," << cw << ") ndc=("
           << (cx * iw) << "," << (cy * iw) << "," << (cz * iw) << ")\n";
}

bool shadowDiagDumpSession(const ShadowDumpContext& ctx)
{
    if (!ctx.device || !ctx.frame || !ctx.resources || !ctx.outputDir || !ctx.outputDir[0] || !ctx.renderView ||
        !ctx.releaseSurface) {
        printf("[dump] invalid dump context\n");
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(ctx.outputDir, ec);
    if (ec) {
        printf("[dump] failed to create output dir: %s (%s)\n", ctx.outputDir, ec.message().c_str());
        return false;
    }

    char pathBuf[1024];
    auto joinPath = [&](const char* leaf) -> const char* {
        snprintf(pathBuf, sizeof(pathBuf), "%s/%s", ctx.outputDir, leaf);
        return pathBuf;
    };

    std::ofstream report(joinPath("report.txt"));
    if (!report) {
        printf("[dump] failed to open report.txt\n");
        return false;
    }

    report << "shadow dump session\n";
    if (ctx.camera) {
        report << "camera pos=(" << ctx.camera->position.x << "," << ctx.camera->position.y << ","
               << ctx.camera->position.z << ") yaw=" << ctx.camera->yaw << " pitch=" << ctx.camera->pitch << "\n";
    }
    if (ctx.boundsMin && ctx.boundsMax) {
        report << "scene bounds min=(" << ctx.boundsMin->x << "," << ctx.boundsMin->y << "," << ctx.boundsMin->z
               << ") max=(" << ctx.boundsMax->x << "," << ctx.boundsMax->y << "," << ctx.boundsMax->z << ")\n";
    }

    for (int ci = 0; ci < 4; ci++) {
        const CascadeShadowData& c = ctx.frame->cascadeShadows[ci];
        report << "cascade[" << ci << "] splitFar=" << c.splitFar << " texelSize=" << c.texelSize << "\n";
        report << "  viewProj row0=(" << c.viewProj[0] << "," << c.viewProj[1] << "," << c.viewProj[2] << ","
               << c.viewProj[3] << ")\n";
    }

    const RenderViewMode kDumpViews[] = {
        RenderViewMode::Final,
        RenderViewMode::Shadow,
        RenderViewMode::ShadowMap,
        RenderViewMode::ShadowAtlas,
        RenderViewMode::ShadowUv,
        RenderViewMode::Material,
        RenderViewMode::Direct,
    };

    bool ok = true;
    RenderViewMode savedMode = ctx.frame->viewMode;
    for (RenderViewMode mode : kDumpViews) {
        GpuSurfaceTexture surf = nullptr;
        if (!ctx.renderView(ctx.renderUserData, mode, &surf) || !surf) {
            report << "view " << renderViewModeName(mode) << " RENDER FAILED\n";
            printf("[dump] FAIL render %s\n", renderViewModeName(mode));
            ok = false;
            continue;
        }

        char fileName[64];
        snprintf(fileName, sizeof(fileName), "view_%s.ppm", renderViewModeName(mode));
        ColorBufferStats colorStats = {};
        if (shadowDiagReadbackSurfaceToPpm(ctx.device, surf, ctx.surfaceWidth, ctx.surfaceHeight, joinPath(fileName),
                                           &colorStats)) {
            report << "view " << renderViewModeName(mode) << " luma min=" << colorStats.minLuma
                   << " max=" << colorStats.maxLuma << " mean=" << colorStats.meanLuma << "\n";
            printf("[dump] wrote %s (mean luma=%.4f)\n", fileName, colorStats.meanLuma);
        } else {
            report << "view " << renderViewModeName(mode) << " READBACK FAILED\n";
            printf("[dump] FAIL %s readback\n", fileName);
            ok = false;
        }
        ctx.releaseSurface(ctx.renderUserData, surf);
    }
    ctx.frame->viewMode = savedMode;

    for (int ci = 0; ci < 4; ci++) {
        char fileName[64];
        snprintf(fileName, sizeof(fileName), "cascade_%d_depth.pgm", ci);
        ShadowDepthStats depthStats = {};
        std::vector<uint8_t> depthBytes;
        uint32_t dw = 0, dh = 0, dPitch = 0;
        if (readbackCascadeDepthRaw(ctx.device, ctx.resources->cascadeDepth[ci], depthBytes, dw, dh, dPitch,
                                    &depthStats)) {
            writePgm16Gray(joinPath(fileName), depthBytes.data(), dPitch, dw, dh, depthStats.minVal,
                           depthStats.maxVal);
            snprintf(fileName, sizeof(fileName), "cascade_%d_depth.raw", ci);
            if (writeRawDepthBin(joinPath(fileName), depthBytes.data(), dPitch, dw, dh)) {
                printf("[dump] wrote %s (%ux%u raw float)\n", fileName, dw, dh);
            }
            report << "cascade[" << ci << "] depth min=" << depthStats.minVal << " max=" << depthStats.maxVal
                   << " nearClear=" << depthStats.pixelsNearClear << " nearZero=" << depthStats.pixelsNearZero
                   << " mid=" << depthStats.pixelsMidRange << "\n";
            printf("[dump] wrote cascade_%d_depth.pgm (depth min=%.6f max=%.6f)\n", ci, depthStats.minVal,
                   depthStats.maxVal);
        } else {
            report << "cascade[" << ci << "] depth READBACK FAILED\n";
            printf("[dump] FAIL cascade_%d_depth readback\n", ci);
            ok = false;
        }
    }

    if (writeDepthCompareCsv(joinPath("depth_compare_c0.csv"), joinPath("view_shadowmap.ppm"),
                             joinPath("view_shadowuv.ppm"), joinPath("cascade_0_depth.raw"), 0)) {
        printf("[dump] wrote depth_compare_c0.csv\n");
    }

    if (ctx.boundsMin && ctx.boundsMax && ctx.camera) {
        Vec3 center = {(ctx.boundsMin->x + ctx.boundsMax->x) * 0.5f,
                       (ctx.boundsMin->y + ctx.boundsMax->y) * 0.5f,
                       (ctx.boundsMin->z + ctx.boundsMax->z) * 0.5f};
        reportCpuClip(report, ctx.frame->cascadeShadows[0], "scene_center", center.x, center.y, center.z);
        reportCpuClip(report, ctx.frame->cascadeShadows[0], "camera", ctx.camera->position.x, ctx.camera->position.y,
                      ctx.camera->position.z);
    }

    report << "done ok=" << (ok ? 1 : 0) << "\n";
    printf("[dump] report written to %s/report.txt\n", ctx.outputDir);
    return ok;
}
