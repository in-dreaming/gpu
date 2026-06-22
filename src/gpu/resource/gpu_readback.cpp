#include "gpu/resource/gpu_readback.h"
#include "gpu/core/gpu_internal.h"

// D3D12 minimum row pitch alignment is 256 bytes
#define GPU_MIN_ROW_PITCH_ALIGNMENT 256

static uint32_t getFormatBytesPerPixel(rhi::Format format)
{
    switch (format) {
    case rhi::Format::R8Unorm:
    case rhi::Format::R8Snorm:
    case rhi::Format::R8Uint:
    case rhi::Format::R8Sint:
        return 1;
    case rhi::Format::RG8Unorm:
    case rhi::Format::RG8Snorm:
    case rhi::Format::RG8Uint:
    case rhi::Format::RG8Sint:
    case rhi::Format::R16Unorm:
    case rhi::Format::R16Snorm:
    case rhi::Format::R16Uint:
    case rhi::Format::R16Sint:
    case rhi::Format::R16Float:
        return 2;
    case rhi::Format::RGBA8Unorm:
    case rhi::Format::RGBA8UnormSrgb:
    case rhi::Format::RGBA8Snorm:
    case rhi::Format::RGBA8Uint:
    case rhi::Format::RGBA8Sint:
    case rhi::Format::BGRA8Unorm:
    case rhi::Format::BGRA8UnormSrgb:
    case rhi::Format::RG16Unorm:
    case rhi::Format::RG16Snorm:
    case rhi::Format::RG16Uint:
    case rhi::Format::RG16Sint:
    case rhi::Format::RG16Float:
    case rhi::Format::R32Uint:
    case rhi::Format::R32Sint:
    case rhi::Format::R32Float:
    case rhi::Format::D16Unorm:
    case rhi::Format::D32Float:
        return 4;
    case rhi::Format::RGBA16Unorm:
    case rhi::Format::RGBA16Snorm:
    case rhi::Format::RGBA16Uint:
    case rhi::Format::RGBA16Sint:
    case rhi::Format::RGBA16Float:
    case rhi::Format::RG32Uint:
    case rhi::Format::RG32Sint:
    case rhi::Format::RG32Float:
    case rhi::Format::D32FloatS8Uint:
        return 8;
    case rhi::Format::RGBA32Uint:
    case rhi::Format::RGBA32Sint:
    case rhi::Format::RGBA32Float:
        return 16;
    default:
        return 4;  // safe default
    }
}

static uint32_t alignTo(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

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

    const auto& texDesc = rhiSrc->getDesc();
    const auto& sz = texDesc.size;

    // Compute proper row pitch with alignment
    uint32_t bpp = getFormatBytesPerPixel(texDesc.format);
    uint32_t rowPitch = alignTo(sz.width * bpp, GPU_MIN_ROW_PITCH_ALIGNMENT);
    uint32_t slicePitch = rowPitch * sz.height;
    uint64_t totalSize = (uint64_t)slicePitch * sz.depth;

    // Insert resource barriers: transition texture to CopySource, buffer to CopyDestination
    encoder->rhiEncoder->setTextureState(rhiSrc, rhi::kEntireTexture, rhi::ResourceState::CopySource);
    encoder->rhiEncoder->setBufferState(rhiDst, rhi::ResourceState::CopyDestination);
    encoder->rhiEncoder->globalBarrier();

    // Copy with proper row pitch
    encoder->rhiEncoder->copyTextureToBuffer(
        rhiDst, dstOffset, totalSize, rowPitch,
        rhiSrc, srcSlice, srcMip,
        {0, 0, 0},
        {sz.width, sz.height, sz.depth}
    );

    // Transition texture back to RenderTarget (common state after render pass)
    encoder->rhiEncoder->setTextureState(rhiSrc, rhi::kEntireTexture, rhi::ResourceState::RenderTarget);
    encoder->rhiEncoder->globalBarrier();

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

// Helper: get the row pitch for a texture format (useful for reading readback data)
uint32_t gpuGetReadbackRowPitch(GpuTextureHandle texture, GpuDevice device)
{
    if (!device || texture.index == 0) return 0;
    rhi::ITexture* rhiTex = device->texturePool.resolve(texture.index, texture.generation);
    if (!rhiTex) return 0;
    const auto& desc = rhiTex->getDesc();
    uint32_t bpp = getFormatBytesPerPixel(desc.format);
    return alignTo(desc.size.width * bpp, GPU_MIN_ROW_PITCH_ALIGNMENT);
}
