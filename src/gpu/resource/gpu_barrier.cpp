#include "gpu/resource/gpu_barrier.h"
#include "gpu/core/gpu_internal.h"
#include <vector>
#include <mutex>

static rhi::ResourceState gpuResourceStateToRhi(GpuResourceState state)
{
    switch (state) {
    case GPU_RESOURCE_STATE_UNDEFINED:       return rhi::ResourceState::Undefined;
    case GPU_RESOURCE_STATE_COMMON:          return rhi::ResourceState::General;
    case GPU_RESOURCE_STATE_VERTEX_BUFFER:   return rhi::ResourceState::VertexBuffer;
    case GPU_RESOURCE_STATE_INDEX_BUFFER:    return rhi::ResourceState::IndexBuffer;
    case GPU_RESOURCE_STATE_CONSTANT_BUFFER: return rhi::ResourceState::ConstantBuffer;
    case GPU_RESOURCE_STATE_SHADER_RESOURCE: return rhi::ResourceState::ShaderResource;
    case GPU_RESOURCE_STATE_UNORDERED_ACCESS:return rhi::ResourceState::UnorderedAccess;
    case GPU_RESOURCE_STATE_RENDER_TARGET:   return rhi::ResourceState::RenderTarget;
    case GPU_RESOURCE_STATE_DEPTH_WRITE:     return rhi::ResourceState::DepthWrite;
    case GPU_RESOURCE_STATE_DEPTH_READ:      return rhi::ResourceState::DepthRead;
    case GPU_RESOURCE_STATE_PRESENT:         return rhi::ResourceState::Present;
    case GPU_RESOURCE_STATE_COPY_SOURCE:     return rhi::ResourceState::CopySource;
    case GPU_RESOURCE_STATE_COPY_DEST:       return rhi::ResourceState::CopyDestination;
    case GPU_RESOURCE_STATE_STORAGE_BUFFER:  return rhi::ResourceState::UnorderedAccess;
    default:                                 return rhi::ResourceState::Undefined;
    }
}

void gpuCmdSetBufferState(GpuDevice device, GpuCommandEncoder encoder, GpuBufferHandle buffer, GpuResourceState state)
{
    if (!device || !encoder || buffer.index == 0) return;
    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!rhiBuf) return;
    encoder->rhiEncoder->setBufferState(rhiBuf, gpuResourceStateToRhi(state));
    device->bufferStates[buffer.index] = state;
}

void gpuCmdSetTextureState(GpuDevice device, GpuCommandEncoder encoder, GpuTextureHandle texture, GpuResourceState state)
{
    if (!device || !encoder || texture.index == 0) return;
    rhi::ITexture* rhiTex = device->texturePool.resolve(texture.index, texture.generation);
    if (!rhiTex) return;
    encoder->rhiEncoder->setTextureState(rhiTex, gpuResourceStateToRhi(state));
    device->textureStates[texture.index] = state;
}

void gpuCmdGlobalBarrier(GpuCommandEncoder encoder)
{
    if (!encoder) return;
    encoder->rhiEncoder->globalBarrier();
}

GpuResourceState gpuGetBufferState(GpuDevice device, GpuBufferHandle buffer)
{
    if (!device || buffer.index == 0) return GPU_RESOURCE_STATE_UNDEFINED;
    if (!device->bufferPool.resolve(buffer.index, buffer.generation)) return GPU_RESOURCE_STATE_UNDEFINED;
    return device->bufferStates[buffer.index];
}

GpuResourceState gpuGetTextureState(GpuDevice device, GpuTextureHandle texture)
{
    if (!device || texture.index == 0) return GPU_RESOURCE_STATE_UNDEFINED;
    if (!device->texturePool.resolve(texture.index, texture.generation)) return GPU_RESOURCE_STATE_UNDEFINED;
    return device->textureStates[texture.index];
}

void gpuCmdSetTextureSubresourceState(GpuDevice device, GpuCommandEncoder encoder,
                                       GpuTextureHandle texture, uint32_t mipLevel,
                                       uint32_t arrayLayer, GpuResourceState state)
{
    if (!device || !encoder || texture.index == 0) return;
    rhi::ITexture* rhiTex = device->texturePool.resolve(texture.index, texture.generation);
    if (!rhiTex) return;
    rhi::SubresourceRange range;
    range.mip = mipLevel;
    range.mipCount = 1;
    range.layer = arrayLayer;
    range.layerCount = 1;
    encoder->rhiEncoder->setTextureState(rhiTex, range, gpuResourceStateToRhi(state));
}

GpuResourceState gpuGetTextureSubresourceState(GpuDevice device, GpuTextureHandle texture,
                                                uint32_t mipLevel, uint32_t arrayLayer)
{
    if (!device || texture.index == 0) return GPU_RESOURCE_STATE_UNDEFINED;
    GpuSubresourceTracker trk = device->subresourceTrackers[texture.index];
    if (trk) return gpuSubresourceTrackerGet(trk, mipLevel, arrayLayer);
    return device->textureStates[texture.index];
}

struct GpuSubresourceStateRecord_t {
    GpuDevice device;
    uint32_t mipCount;
    uint32_t arrayLength;
    std::vector<GpuResourceState> states;
    std::mutex mtx;

    GpuResourceState get(uint32_t mip, uint32_t layer)
    {
        if (mip >= mipCount || layer >= arrayLength) return GPU_RESOURCE_STATE_UNDEFINED;
        return states[mip * arrayLength + layer];
    }

    void set(uint32_t mip, uint32_t layer, GpuResourceState s)
    {
        if (mip >= mipCount || layer >= arrayLength) return;
        states[mip * arrayLength + layer] = s;
    }
};

GpuResult gpuSubresourceTrackerCreate(GpuDevice device, GpuTextureHandle texture, GpuSubresourceTracker* out)
{
    if (!device || !out) return GPU_ERROR_INVALID_ARGS;
    rhi::ITexture* tex = device->texturePool.resolve(texture.index, texture.generation);
    if (!tex) return GPU_ERROR_INVALID_ARGS;

    auto* trk = new GpuSubresourceStateRecord_t();
    trk->device = device;
    const auto& desc = tex->getDesc();
    trk->mipCount = desc.mipCount ? desc.mipCount : 1;
    trk->arrayLength = desc.arrayLength ? desc.arrayLength : 1;
    trk->states.assign(trk->mipCount * trk->arrayLength, GPU_RESOURCE_STATE_UNDEFINED);

    device->subresourceTrackers[texture.index] = trk;
    *out = trk;
    return GPU_SUCCESS;
}

void gpuSubresourceTrackerDestroy(GpuSubresourceTracker tracker)
{
    if (!tracker) return;
    auto* trk = static_cast<GpuSubresourceStateRecord_t*>(tracker);
    if (trk->device) {
        for (uint32_t i = 0; i < GpuHandlePool<rhi::ITexture>::capacity(); i++) {
            if (trk->device->subresourceTrackers[i] == trk) {
                trk->device->subresourceTrackers[i] = nullptr;
                break;
            }
        }
    }
    delete trk;
}

GpuResourceState gpuSubresourceTrackerGet(GpuSubresourceTracker tracker, uint32_t mip, uint32_t layer)
{
    if (!tracker) return GPU_RESOURCE_STATE_UNDEFINED;
    auto* trk = static_cast<GpuSubresourceStateRecord_t*>(tracker);
    std::lock_guard<std::mutex> lk(trk->mtx);
    return trk->get(mip, layer);
}

void gpuSubresourceTrackerSet(GpuSubresourceTracker tracker, uint32_t mip, uint32_t layer, GpuResourceState state)
{
    if (!tracker) return;
    auto* trk = static_cast<GpuSubresourceStateRecord_t*>(tracker);
    std::lock_guard<std::mutex> lk(trk->mtx);
    trk->set(mip, layer, state);
}
