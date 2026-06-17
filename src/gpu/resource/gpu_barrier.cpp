#include "gpu/resource/gpu_barrier.h"
#include "gpu/core/gpu_internal.h"

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
}

void gpuCmdSetTextureState(GpuDevice device, GpuCommandEncoder encoder, GpuTextureHandle texture, GpuResourceState state)
{
    if (!device || !encoder || texture.index == 0) return;
    rhi::ITexture* rhiTex = device->texturePool.resolve(texture.index, texture.generation);
    if (!rhiTex) return;
    encoder->rhiEncoder->setTextureState(rhiTex, gpuResourceStateToRhi(state));
}

void gpuCmdGlobalBarrier(GpuCommandEncoder encoder)
{
    if (!encoder) return;
    encoder->rhiEncoder->globalBarrier();
}
