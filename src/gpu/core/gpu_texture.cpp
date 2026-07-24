#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/debug/gpu_validation.h"
#include "gpu/resource/gpu_frame_context.h"
#include "gpu/resource/gpu_readback.h"
#include "gpu/resource/gpu_barrier.h"
#include "gpu/bindless/gpu_bindless_heap.h"

static GpuResourceState gpuDefaultTextureState(GpuTextureUsage usage)
{
    const GpuTextureUsage statefulUsage = usage & (
        GPU_TEXTURE_USAGE_UNORDERED_ACCESS |
        GPU_TEXTURE_USAGE_RENDER_TARGET |
        GPU_TEXTURE_USAGE_DEPTH_STENCIL |
        GPU_TEXTURE_USAGE_PRESENT |
        GPU_TEXTURE_USAGE_SHADER_RESOURCE |
        GPU_TEXTURE_USAGE_COPY_DEST |
        GPU_TEXTURE_USAGE_COPY_SOURCE);
    if (statefulUsage && (statefulUsage & (statefulUsage - 1)) != 0)
        return GPU_RESOURCE_STATE_COMMON;
    if (usage & GPU_TEXTURE_USAGE_UNORDERED_ACCESS) return GPU_RESOURCE_STATE_UNORDERED_ACCESS;
    if (usage & GPU_TEXTURE_USAGE_RENDER_TARGET) return GPU_RESOURCE_STATE_RENDER_TARGET;
    if (usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL) return GPU_RESOURCE_STATE_DEPTH_WRITE;
    if (usage & GPU_TEXTURE_USAGE_PRESENT) return GPU_RESOURCE_STATE_PRESENT;
    if (usage & GPU_TEXTURE_USAGE_SHADER_RESOURCE) return GPU_RESOURCE_STATE_SHADER_RESOURCE;
    if (usage & GPU_TEXTURE_USAGE_COPY_DEST) return GPU_RESOURCE_STATE_COPY_DEST;
    if (usage & GPU_TEXTURE_USAGE_COPY_SOURCE) return GPU_RESOURCE_STATE_COPY_SOURCE;
    return GPU_RESOURCE_STATE_COMMON;
}

static rhi::ResourceState gpuDefaultTextureRhiState(GpuTextureUsage usage)
{
    switch (gpuDefaultTextureState(usage)) {
    case GPU_RESOURCE_STATE_UNORDERED_ACCESS: return rhi::ResourceState::UnorderedAccess;
    case GPU_RESOURCE_STATE_RENDER_TARGET:    return rhi::ResourceState::RenderTarget;
    case GPU_RESOURCE_STATE_DEPTH_WRITE:      return rhi::ResourceState::DepthWrite;
    case GPU_RESOURCE_STATE_PRESENT:          return rhi::ResourceState::Present;
    case GPU_RESOURCE_STATE_SHADER_RESOURCE:  return rhi::ResourceState::ShaderResource;
    case GPU_RESOURCE_STATE_COPY_DEST:        return rhi::ResourceState::CopyDestination;
    case GPU_RESOURCE_STATE_COPY_SOURCE:      return rhi::ResourceState::CopySource;
    default:                                  return rhi::ResourceState::General;
    }
}

GpuResult gpuCreateTexture(GpuDevice device, const GpuTextureDesc* desc, GpuTextureHandle* outHandle)
{
    if (!device || !desc || !outHandle) return GPU_ERROR_INVALID_ARGS;
    if (desc->width == 0 || desc->height == 0) {
        GPU_VALIDATE(device, GPU_VALIDATION_SEVERITY_ERROR, "INVALID_TEXTURE_SIZE",
                     "Texture width and height must be > 0", desc->label);
        return GPU_ERROR_INVALID_ARGS;
    }
    if ((desc->usage & GPU_TEXTURE_USAGE_SPARSE) != 0) {
        GPU_FEATURE_GATE(device, GPU_FEATURE_SPARSE_RESOURCE, desc->label);
    }

    rhi::TextureDesc rhiDesc = {};
    rhiDesc.type = gpuTextureTypeToRhi(desc->type);
    rhiDesc.size.width = desc->width;
    rhiDesc.size.height = desc->height;
    rhiDesc.size.depth = desc->depth > 0 ? desc->depth : 1;
    rhiDesc.arrayLength = desc->arrayLength > 0 ? desc->arrayLength : 1;
    rhiDesc.mipCount = desc->mipCount > 0 ? desc->mipCount : 1;
    rhiDesc.format = gpuFormatToRhi(desc->format);
    rhiDesc.sampleCount = desc->sampleCount > 0 ? desc->sampleCount : 1;
    GpuTextureUsage implementationUsage = desc->usage;
    if ((implementationUsage & GPU_TEXTURE_USAGE_RENDER_TARGET) != 0 &&
        (implementationUsage & GPU_TEXTURE_USAGE_UNORDERED_ACCESS) != 0) {
        implementationUsage = static_cast<GpuTextureUsage>(
            implementationUsage | GPU_TEXTURE_USAGE_COPY_DEST);
    }
    rhiDesc.usage = static_cast<rhi::TextureUsage>(
        gpuTextureUsageToRhi(implementationUsage));
    rhiDesc.defaultState = gpuDefaultTextureRhiState(desc->usage);
    rhiDesc.label = desc->label;
    rhiDesc.memoryType = rhi::MemoryType::DeviceLocal;

    rhi::ComPtr<rhi::ITexture> rhiTexture;
    rhi::Result rhiRes = device->rhiDevice->createTexture(rhiDesc, nullptr, rhiTexture.writeRef());
    if (SLANG_FAILED(rhiRes)) {
        *outHandle = GpuHandle{0, 0};
        return GPU_ERROR_INTERNAL;
    }

    uint32_t idx = device->texturePool.allocate(rhiTexture.detach());
    if (idx == 0) {
        *outHandle = GpuHandle{0, 0};
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outHandle->index = idx;
    outHandle->generation = device->texturePool.slots[idx].generation;
    device->textureStates[idx] = gpuDefaultTextureState(desc->usage);
    return GPU_SUCCESS;
}

GpuResult gpuUploadTextureData(
    GpuDevice device,
    GpuTextureHandle texture,
    const GpuTextureUploadDesc* upload)
{
    if (!device || !gpuHandleIsValid(texture) || !upload || !upload->data) {
        return GPU_ERROR_INVALID_ARGS;
    }

    rhi::ITexture* rhiTexture = device->texturePool.resolve(texture.index, texture.generation);
    if (!rhiTexture) return GPU_ERROR_INVALID_ARGS;
    const auto& textureDesc = rhiTexture->getDesc();
    if (upload->mipLevel >= textureDesc.mipCount ||
        upload->arrayLayer >= textureDesc.arrayLength) {
        return GPU_ERROR_INVALID_ARGS;
    }

    GpuTextureFootprint footprint = {};
    GpuResult footprintResult =
        gpuGetTextureReadbackFootprint(device, texture, upload->mipLevel, &footprint);
    if (footprintResult != GPU_SUCCESS) return footprintResult;
    const uint32_t bytesPerPixel =
        rhi::getFormatInfo(textureDesc.format).blockSizeInBytes;
    if (bytesPerPixel == 0) return GPU_ERROR_NOT_SUPPORTED;
    const uint32_t minimumRowPitch = footprint.width * bytesPerPixel;
    const uint32_t sourceRowPitch = upload->rowPitch ? upload->rowPitch : minimumRowPitch;
    const uint32_t sourceSlicePitch =
        upload->slicePitch ? upload->slicePitch : sourceRowPitch * footprint.height;
    if (sourceRowPitch < minimumRowPitch ||
        sourceSlicePitch < sourceRowPitch * footprint.height ||
        upload->dataSize < (uint64_t)sourceSlicePitch * footprint.depth) {
        return GPU_ERROR_INVALID_ARGS;
    }

    GpuCommandQueue queue = nullptr;
    if (gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue) != GPU_SUCCESS) {
        return GPU_ERROR_INTERNAL;
    }
    GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
    if (!encoder) return GPU_ERROR_INTERNAL;

    const GpuResourceState originalState = device->textureStates[texture.index];
    gpuCmdSetTextureState(device, encoder, texture, GPU_RESOURCE_STATE_COPY_DEST);
    gpuCmdGlobalBarrier(encoder);

    rhi::SubresourceRange range = {};
    range.mip = upload->mipLevel;
    range.mipCount = 1;
    range.layer = upload->arrayLayer;
    range.layerCount = 1;
    rhi::SubresourceData data = {};
    data.data = upload->data;
    data.rowPitch = sourceRowPitch;
    data.slicePitch = sourceSlicePitch;
    const rhi::Result uploadResult = encoder->rhiEncoder->uploadTextureData(
        rhiTexture,
        range,
        {0, 0, 0},
        {footprint.width, footprint.height, footprint.depth},
        &data,
        1);
    if (SLANG_FAILED(uploadResult)) {
        device->textureStates[texture.index] = originalState;
        gpuCancelCommandEncoder(encoder);
        return GPU_ERROR_INTERNAL;
    }

    gpuCmdSetTextureState(device, encoder, texture, originalState);
    gpuCmdGlobalBarrier(encoder);
    GpuCommandBuffer commands = gpuFinishCommandEncoder(encoder);
    if (!commands) return GPU_ERROR_INTERNAL;
    GpuResult result = gpuQueueSubmit(queue, 1, &commands);
    if (result != GPU_SUCCESS) return result;
    return gpuQueueWaitOnHost(queue);
}

GpuResult gpuDestroyTexture(GpuDevice device, GpuTextureHandle handle)
{
    if (!device || !gpuHandleIsValid(handle)) return GPU_ERROR_INVALID_ARGS;

    rhi::ITexture* tex = device->texturePool.resolve(handle.index, handle.generation);
    if (!tex) return GPU_ERROR_INVALID_ARGS;

    // Invalidate any bindless slots referencing this resource
    {
        GpuBindlessHeap heapToFree = nullptr;
        uint32_t idxToFree = UINT32_MAX;
        {
            auto key = ((uint64_t)handle.index << 32) | handle.generation;
            std::lock_guard<std::mutex> lock(device->bindlessMutex);
            auto it = device->bindlessResourceMap.find(key);
            if (it != device->bindlessResourceMap.end()) {
                heapToFree = it->second.heap;
                idxToFree = it->second.index;
                device->bindlessResourceMap.erase(it);
            }
        }
        if (heapToFree) {
            gpuBindlessFree(heapToFree, idxToFree);
        }
    }

    if (device->frameContext) {
        gpuFrameDeferDestroyTexture(device->frameContext, handle);
        return GPU_SUCCESS;
    }

    // Non-frame path: release directly without waitOnHost.
    tex->release();
    device->textureStates[handle.index] = GPU_RESOURCE_STATE_UNDEFINED;
    device->texturePool.release(handle.index, handle.generation);
    return GPU_SUCCESS;
}

static GpuResult gpuCreateTextureViewInternal(
    GpuDevice device,
    GpuTextureHandle texture,
    GpuTextureViewType type,
    const GpuTextureSubresourceRange* subresource,
    GpuTextureHandle* outViewHandle)
{
    if (!device || !gpuHandleIsValid(texture) || !outViewHandle) return GPU_ERROR_INVALID_ARGS;

    rhi::ITexture* tex = device->texturePool.resolve(texture.index, texture.generation);
    if (!tex) return GPU_ERROR_INVALID_ARGS;

    const auto& texDesc = tex->getDesc();
    rhi::TextureViewDesc viewDesc = {};
    viewDesc.format = texDesc.format;
    if (subresource) {
        viewDesc.subresourceRange.mip = subresource->mip;
        viewDesc.subresourceRange.mipCount = subresource->mipCount > 0 ? subresource->mipCount : 1;
        viewDesc.subresourceRange.layer = subresource->layer;
        viewDesc.subresourceRange.layerCount = subresource->layerCount > 0 ? subresource->layerCount : 1;
    } else {
        viewDesc.subresourceRange.mip = 0;
        viewDesc.subresourceRange.mipCount = texDesc.mipCount;
        viewDesc.subresourceRange.layer = 0;
        viewDesc.subresourceRange.layerCount = texDesc.arrayLength;
    }

    // Set aspect based on view type
    if (type == GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL) {
        viewDesc.aspect = rhi::TextureAspect::DepthOnly;
    } else if (type == GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE || type == GPU_TEXTURE_VIEW_TYPE_UNORDERED_ACCESS) {
        auto fmt = tex->getDesc().format;
        if (fmt == rhi::Format::D32Float) {
            viewDesc.aspect = rhi::TextureAspect::DepthOnly;
            viewDesc.format = rhi::Format::R32Float;
        } else if (fmt == rhi::Format::D16Unorm) {
            viewDesc.aspect = rhi::TextureAspect::DepthOnly;
            viewDesc.format = rhi::Format::R16Unorm;
        } else if (fmt == rhi::Format::D32FloatS8Uint) {
            viewDesc.aspect = rhi::TextureAspect::DepthOnly;
            viewDesc.format = rhi::Format::R32Float;
        }
    } else {
        viewDesc.aspect = rhi::TextureAspect::All;
    }

    rhi::ComPtr<rhi::ITextureView> rhiView;
    rhi::Result rhiRes = device->rhiDevice->createTextureView(tex, viewDesc, rhiView.writeRef());
    if (SLANG_FAILED(rhiRes)) {
        *outViewHandle = GpuHandle{0, 0};
        return GPU_ERROR_INTERNAL;
    }

    uint32_t idx = device->textureViewPool.allocate(rhiView.detach());
    if (idx == 0) {
        *outViewHandle = GpuHandle{0, 0};
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outViewHandle->index = idx;
    outViewHandle->generation = device->textureViewPool.slots[idx].generation;
    return GPU_SUCCESS;
}

GpuResult gpuCreateTextureView(GpuDevice device, GpuTextureHandle texture, GpuTextureViewType type, GpuTextureHandle* outViewHandle)
{
    return gpuCreateTextureViewInternal(device, texture, type, nullptr, outViewHandle);
}

GpuResult gpuCreateTextureSubresourceView(
    GpuDevice device,
    GpuTextureHandle texture,
    GpuTextureViewType type,
    const GpuTextureSubresourceRange* subresource,
    GpuTextureHandle* outViewHandle)
{
    if (!subresource) return GPU_ERROR_INVALID_ARGS;
    return gpuCreateTextureViewInternal(device, texture, type, subresource, outViewHandle);
}

GpuResult gpuDestroyTextureView(GpuDevice device, GpuTextureHandle viewHandle)
{
    if (!device || !gpuHandleIsValid(viewHandle)) return GPU_ERROR_INVALID_ARGS;

    rhi::ITextureView* view = device->textureViewPool.resolve(viewHandle.index, viewHandle.generation);
    if (!view) return GPU_ERROR_INVALID_ARGS;

    if (device->frameContext) {
        gpuFrameDeferDestroyTextureView(device->frameContext, viewHandle);
        return GPU_SUCCESS;
    }

    // Non-frame path: release directly without waitOnHost.
    view->release();
    device->textureViewPool.release(viewHandle.index, viewHandle.generation);
    return GPU_SUCCESS;
}
