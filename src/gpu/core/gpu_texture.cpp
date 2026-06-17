#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_internal.h"

GpuResult gpuCreateTexture(GpuDevice device, const GpuTextureDesc* desc, GpuTextureHandle* outHandle)
{
    if (!device || !desc || !outHandle) return GPU_ERROR_INVALID_ARGS;
    if (desc->width == 0 || desc->height == 0) return GPU_ERROR_INVALID_ARGS;

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
    rhiDesc.defaultState = rhi::ResourceState::RenderTarget;
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
    return GPU_SUCCESS;
}

GpuResult gpuDestroyTexture(GpuDevice device, GpuTextureHandle handle)
{
    if (!device || !gpuHandleIsValid(handle)) return GPU_ERROR_INVALID_ARGS;

    rhi::ITexture* tex = device->texturePool.resolve(handle.index, handle.generation);
    if (!tex) return GPU_ERROR_INVALID_ARGS;

    tex->release();
    device->texturePool.release(handle.index, handle.generation);
    return GPU_SUCCESS;
}

GpuResult gpuCreateTextureView(GpuDevice device, GpuTextureHandle texture, GpuTextureViewType type, GpuTextureHandle* outViewHandle)
{
    (void)type;  // Type parameter reserved for future use
    if (!device || !gpuHandleIsValid(texture) || !outViewHandle) return GPU_ERROR_INVALID_ARGS;

    rhi::ITexture* tex = device->texturePool.resolve(texture.index, texture.generation);
    if (!tex) return GPU_ERROR_INVALID_ARGS;

    rhi::TextureViewDesc viewDesc = {};
    viewDesc.format = tex->getDesc().format;
    viewDesc.subresourceRange.mip = 0;
    viewDesc.subresourceRange.mipCount = 1;
    viewDesc.subresourceRange.layer = 0;
    viewDesc.subresourceRange.layerCount = 1;

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

    view->release();
    device->textureViewPool.release(viewHandle.index, viewHandle.generation);
    return GPU_SUCCESS;
}
