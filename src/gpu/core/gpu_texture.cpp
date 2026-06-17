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
