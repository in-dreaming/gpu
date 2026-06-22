#include "gpu/debug/gpu_debug_markers.h"
#include "gpu/core/gpu_internal.h"
#include <string>
#include <map>
#include <mutex>

// ============================================================================
// Internal storage for debug names set after creation
// ============================================================================

static std::mutex g_debugNameMutex;
static std::map<uint64_t, std::string> g_bufferDebugNames;
static std::map<uint64_t, std::string> g_textureDebugNames;
static std::map<uint64_t, std::string> g_samplerDebugNames;

static inline uint64_t makeKey(uint32_t index, uint32_t generation) {
    return ((uint64_t)index << 32) | generation;
}

// ============================================================================
// Debug Markers
// ============================================================================

void gpuCmdPushDebugGroup(GpuCommandEncoder encoder, const char* name, GpuMarkerColor color)
{
    if (!encoder || !name) return;
    rhi::MarkerColor c = { color.r, color.g, color.b };
    encoder->rhiEncoder->pushDebugGroup(name, c);
}

void gpuCmdPopDebugGroup(GpuCommandEncoder encoder)
{
    if (!encoder) return;
    encoder->rhiEncoder->popDebugGroup();
}

void gpuCmdInsertDebugMarker(GpuCommandEncoder encoder, const char* name, GpuMarkerColor color)
{
    if (!encoder || !name) return;
    rhi::MarkerColor c = { color.r, color.g, color.b };
    encoder->rhiEncoder->insertDebugMarker(name, c);
}

void gpuCmdPushRenderDebugGroup(GpuRenderPassEncoder pass, const char* name, GpuMarkerColor color)
{
    if (!pass || !name) return;
    rhi::MarkerColor c = { color.r, color.g, color.b };
    pass->rhiPassEncoder->pushDebugGroup(name, c);
}

void gpuCmdPopRenderDebugGroup(GpuRenderPassEncoder pass)
{
    if (!pass) return;
    pass->rhiPassEncoder->popDebugGroup();
}

void gpuCmdInsertRenderDebugMarker(GpuRenderPassEncoder pass, const char* name, GpuMarkerColor color)
{
    if (!pass || !name) return;
    rhi::MarkerColor c = { color.r, color.g, color.b };
    pass->rhiPassEncoder->insertDebugMarker(name, c);
}

void gpuCmdPushComputeDebugGroup(GpuComputePassEncoder pass, const char* name, GpuMarkerColor color)
{
    if (!pass || !name) return;
    auto* rhiPass = reinterpret_cast<rhi::IComputePassEncoder*>(pass);
    rhi::MarkerColor c = { color.r, color.g, color.b };
    rhiPass->pushDebugGroup(name, c);
}

void gpuCmdPopComputeDebugGroup(GpuComputePassEncoder pass)
{
    if (!pass) return;
    auto* rhiPass = reinterpret_cast<rhi::IComputePassEncoder*>(pass);
    rhiPass->popDebugGroup();
}

void gpuCmdInsertComputeDebugMarker(GpuComputePassEncoder pass, const char* name, GpuMarkerColor color)
{
    if (!pass || !name) return;
    auto* rhiPass = reinterpret_cast<rhi::IComputePassEncoder*>(pass);
    rhi::MarkerColor c = { color.r, color.g, color.b };
    rhiPass->insertDebugMarker(name, c);
}

// ============================================================================
// Debug Names
// ============================================================================

GpuResult gpuSetBufferDebugName(GpuDevice device, GpuBufferHandle buffer, const char* name)
{
    if (!device || buffer.index == 0) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(g_debugNameMutex);
    if (name) {
        g_bufferDebugNames[makeKey(buffer.index, buffer.generation)] = name;
    } else {
        g_bufferDebugNames.erase(makeKey(buffer.index, buffer.generation));
    }
    return GPU_SUCCESS;
}

GpuResult gpuSetTextureDebugName(GpuDevice device, GpuTextureHandle texture, const char* name)
{
    if (!device || texture.index == 0) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(g_debugNameMutex);
    if (name) {
        g_textureDebugNames[makeKey(texture.index, texture.generation)] = name;
    } else {
        g_textureDebugNames.erase(makeKey(texture.index, texture.generation));
    }
    return GPU_SUCCESS;
}

GpuResult gpuSetSamplerDebugName(GpuDevice device, GpuSamplerHandle sampler, const char* name)
{
    if (!device || sampler.index == 0) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(g_debugNameMutex);
    if (name) {
        g_samplerDebugNames[makeKey(sampler.index, sampler.generation)] = name;
    } else {
        g_samplerDebugNames.erase(makeKey(sampler.index, sampler.generation));
    }
    return GPU_SUCCESS;
}

const char* gpuGetBufferDebugName(GpuDevice device, GpuBufferHandle buffer)
{
    if (!device || buffer.index == 0) return nullptr;
    std::lock_guard<std::mutex> lock(g_debugNameMutex);
    auto it = g_bufferDebugNames.find(makeKey(buffer.index, buffer.generation));
    if (it != g_bufferDebugNames.end()) return it->second.c_str();

    // Fall back to creation label from RHI desc
    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(buffer.index, buffer.generation);
    if (rhiBuf) return rhiBuf->getDesc().label;
    return nullptr;
}

const char* gpuGetTextureDebugName(GpuDevice device, GpuTextureHandle texture)
{
    if (!device || texture.index == 0) return nullptr;
    std::lock_guard<std::mutex> lock(g_debugNameMutex);
    auto it = g_textureDebugNames.find(makeKey(texture.index, texture.generation));
    if (it != g_textureDebugNames.end()) return it->second.c_str();

    rhi::ITexture* rhiTex = device->texturePool.resolve(texture.index, texture.generation);
    if (rhiTex) return rhiTex->getDesc().label;
    return nullptr;
}

const char* gpuGetSamplerDebugName(GpuDevice device, GpuSamplerHandle sampler)
{
    if (!device || sampler.index == 0) return nullptr;
    std::lock_guard<std::mutex> lock(g_debugNameMutex);
    auto it = g_samplerDebugNames.find(makeKey(sampler.index, sampler.generation));
    if (it != g_samplerDebugNames.end()) return it->second.c_str();

    rhi::ISampler* rhiSampler = device->samplerPool.resolve(sampler.index, sampler.generation);
    if (rhiSampler) return rhiSampler->getDesc().label;
    return nullptr;
}
