#ifdef _MSC_VER
#pragma warning(disable : 4819)
#endif
#include "gpu/debug/gpu_debug_markers.h"
#include "gpu/core/gpu_internal.h"
#include <string>

// ============================================================================
// Internal helpers
// ============================================================================

static inline uint64_t makeKey(uint32_t index, uint32_t generation, uint32_t typeTag) {
    return ((uint64_t)typeTag << 48) | ((uint64_t)index << 16) | (uint64_t)generation;
}

// Type tags for debug name keys (avoids collisions between buffer/texture/sampler with same index)
static const uint32_t NAME_TAG_BUFFER  = 1;
static const uint32_t NAME_TAG_TEXTURE = 2;
static const uint32_t NAME_TAG_SAMPLER = 3;

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
// Debug Names — per-device storage (Phase E fix: was global static)
// ============================================================================

GpuResult gpuSetBufferDebugName(GpuDevice device, GpuBufferHandle buffer, const char* name)
{
    if (!device || buffer.index == 0) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(device->debugNameMutex);
    auto key = makeKey(buffer.index, buffer.generation, NAME_TAG_BUFFER);
    if (name) {
        device->debugNames[key] = name;
    } else {
        device->debugNames.erase(key);
    }
    return GPU_SUCCESS;
}

GpuResult gpuSetTextureDebugName(GpuDevice device, GpuTextureHandle texture, const char* name)
{
    if (!device || texture.index == 0) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(device->debugNameMutex);
    auto key = makeKey(texture.index, texture.generation, NAME_TAG_TEXTURE);
    if (name) {
        device->debugNames[key] = name;
    } else {
        device->debugNames.erase(key);
    }
    return GPU_SUCCESS;
}

GpuResult gpuSetSamplerDebugName(GpuDevice device, GpuSamplerHandle sampler, const char* name)
{
    if (!device || sampler.index == 0) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(device->debugNameMutex);
    auto key = makeKey(sampler.index, sampler.generation, NAME_TAG_SAMPLER);
    if (name) {
        device->debugNames[key] = name;
    } else {
        device->debugNames.erase(key);
    }
    return GPU_SUCCESS;
}

const char* gpuGetBufferDebugName(GpuDevice device, GpuBufferHandle buffer)
{
    if (!device || buffer.index == 0) return nullptr;
    std::lock_guard<std::mutex> lock(device->debugNameMutex);
    auto it = device->debugNames.find(makeKey(buffer.index, buffer.generation, NAME_TAG_BUFFER));
    if (it != device->debugNames.end()) return it->second.c_str();

    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(buffer.index, buffer.generation);
    if (rhiBuf) return rhiBuf->getDesc().label;
    return nullptr;
}

const char* gpuGetTextureDebugName(GpuDevice device, GpuTextureHandle texture)
{
    if (!device || texture.index == 0) return nullptr;
    std::lock_guard<std::mutex> lock(device->debugNameMutex);
    auto it = device->debugNames.find(makeKey(texture.index, texture.generation, NAME_TAG_TEXTURE));
    if (it != device->debugNames.end()) return it->second.c_str();

    rhi::ITexture* rhiTex = device->texturePool.resolve(texture.index, texture.generation);
    if (rhiTex) return rhiTex->getDesc().label;
    return nullptr;
}

const char* gpuGetSamplerDebugName(GpuDevice device, GpuSamplerHandle sampler)
{
    if (!device || sampler.index == 0) return nullptr;
    std::lock_guard<std::mutex> lock(device->debugNameMutex);
    auto it = device->debugNames.find(makeKey(sampler.index, sampler.generation, NAME_TAG_SAMPLER));
    if (it != device->debugNames.end()) return it->second.c_str();

    rhi::ISampler* rhiSampler = device->samplerPool.resolve(sampler.index, sampler.generation);
    if (rhiSampler) return rhiSampler->getDesc().label;
    return nullptr;
}
