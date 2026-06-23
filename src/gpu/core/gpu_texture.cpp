#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/debug/gpu_validation.h"
#include "gpu/resource/gpu_frame_context.h"
#include "gpu/bindless/gpu_bindless_heap.h"

static GpuResourceState gpuDefaultTextureState(GpuTextureUsage usage)
{
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
    rhiDesc.usage = static_cast<rhi::TextureUsage>(gpuTextureUsageToRhi(desc->usage));
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

GpuResult gpuCreateTextureView(GpuDevice device, GpuTextureHandle texture, GpuTextureViewType type, GpuTextureHandle* outViewHandle)
{
    if (!device || !gpuHandleIsValid(texture) || !outViewHandle) return GPU_ERROR_INVALID_ARGS;

    rhi::ITexture* tex = device->texturePool.resolve(texture.index, texture.generation);
    if (!tex) return GPU_ERROR_INVALID_ARGS;

    rhi::TextureViewDesc viewDesc = {};
    viewDesc.format = tex->getDesc().format;
    viewDesc.subresourceRange.mip = 0;
    viewDesc.subresourceRange.mipCount = tex->getDesc().mipCount;
    viewDesc.subresourceRange.layer = 0;
    viewDesc.subresourceRange.layerCount = tex->getDesc().arrayLength;

    // Set aspect based on view type
    if (type == GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL) {
        viewDesc.aspect = rhi::TextureAspect::DepthOnly;
    } else if (type == GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE || type == GPU_TEXTURE_VIEW_TYPE_UNORDERED_ACCESS) {
        // For depth textures used as SRV, still need DepthOnly aspect
        auto fmt = tex->getDesc().format;
        if (fmt == rhi::Format::D32Float || fmt == rhi::Format::D16Unorm || fmt == rhi::Format::D32FloatS8Uint) {
            viewDesc.aspect = rhi::TextureAspect::DepthOnly;
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
