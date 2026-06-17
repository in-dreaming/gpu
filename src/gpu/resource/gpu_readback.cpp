#include "gpu/resource/gpu_readback.h"
#include "gpu/core/gpu_internal.h"

GpuResult gpuCreateReadbackBuffer(GpuDevice device, uint64_t size, GpuBufferHandle* outHandle)
{
    if (!device || !outHandle || size == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::BufferDesc rhiDesc = {};
    rhiDesc.size = size;
    rhiDesc.usage = rhi::BufferUsage::CopyDestination;
    rhiDesc.defaultState = rhi::ResourceState::CopyDestination;
    rhiDesc.memoryType = rhi::MemoryType::ReadBack;
    rhiDesc.label = "readback";

    rhi::ComPtr<rhi::IBuffer> rhiBuf;
    if (SLANG_FAILED(device->rhiDevice->createBuffer(rhiDesc, nullptr, rhiBuf.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    uint32_t idx = device->bufferPool.allocate(rhiBuf.detach());
    if (idx == 0) return GPU_ERROR_OUT_OF_MEMORY;

    *outHandle = {idx, device->bufferPool.slots[idx].generation};
    return GPU_SUCCESS;
}

GpuResult gpuCmdCopyTextureToBuffer(GpuCommandEncoder encoder,
                                     GpuTextureHandle src,
                                     uint32_t srcMip, uint32_t srcSlice,
                                     GpuBufferHandle dst,
                                     uint64_t dstOffset)
{
    if (!encoder) return GPU_ERROR_INVALID_ARGS;

    rhi::ITexture* rhiSrc = encoder->device->texturePool.resolve(src.index, src.generation);
    rhi::IBuffer* rhiDst = encoder->device->bufferPool.resolve(dst.index, dst.generation);
    if (!rhiSrc || !rhiDst) return GPU_ERROR_INVALID_ARGS;

    const auto& sz = rhiSrc->getDesc().size;
    encoder->rhiEncoder->copyTextureToBuffer(
        rhiDst, dstOffset, 0, 0,
        rhiSrc, srcSlice, srcMip,
        {0, 0, 0},
        {sz.width, sz.height, sz.depth}
    );
    return GPU_SUCCESS;
}

GpuResult gpuMapReadbackBuffer(GpuDevice device, GpuBufferHandle handle, void** outPtr)
{
    if (!device || !outPtr) return GPU_ERROR_INVALID_ARGS;
    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(handle.index, handle.generation);
    if (!rhiBuf) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(device->rhiDevice->mapBuffer(rhiBuf, rhi::CpuAccessMode::Read, outPtr))
        ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}

void gpuUnmapReadbackBuffer(GpuDevice device, GpuBufferHandle handle)
{
    if (!device) return;
    rhi::IBuffer* rhiBuf = device->bufferPool.resolve(handle.index, handle.generation);
    if (!rhiBuf) return;
    device->rhiDevice->unmapBuffer(rhiBuf);
}
