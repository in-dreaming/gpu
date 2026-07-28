#include "gpu/resource/gpu_readback.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/resource/gpu_barrier.h"
#include <algorithm>

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
    case rhi::Format::D16Unorm:
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
    case rhi::Format::R11G11B10Float:
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

static GpuFormat getGpuFormat(rhi::Format format)
{
    for (int value = GPU_FORMAT_R8_UINT; value <= GPU_FORMAT_R11G11B10_FLOAT; ++value) {
        const GpuFormat candidate = static_cast<GpuFormat>(value);
        if (gpuFormatToRhi(candidate) == format) return candidate;
    }
    return GPU_FORMAT_UNDEFINED;
}

static uint32_t alignTo(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static GpuResult getFootprint(
    GpuFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t depth,
    uint32_t mipCount,
    uint32_t mipLevel,
    GpuTextureFootprint* outFootprint)
{
    if (!outFootprint || width == 0 || height == 0 || depth == 0 || mipCount == 0 ||
        mipLevel >= mipCount || format == GPU_FORMAT_UNDEFINED)
        return GPU_ERROR_INVALID_ARGS;
    width = std::max(1u, width >> mipLevel);
    height = std::max(1u, height >> mipLevel);
    depth = std::max(1u, depth >> mipLevel);
    const rhi::Format rhiFormat = gpuFormatToRhi(format);
    if (rhiFormat == rhi::Format::Undefined) return GPU_ERROR_NOT_SUPPORTED;
    const uint32_t rowPitch =
        alignTo(width * getFormatBytesPerPixel(rhiFormat), GPU_MIN_ROW_PITCH_ALIGNMENT);
    const uint32_t slicePitch = rowPitch * height;
    *outFootprint = {
        format,
        width,
        height,
        depth,
        rowPitch,
        slicePitch,
        (uint64_t)slicePitch * depth,
    };
    return GPU_SUCCESS;
}

static GpuResult getTextureFootprint(
    rhi::ITexture* texture,
    uint32_t mipLevel,
    GpuTextureFootprint* outFootprint)
{
    if (!texture) return GPU_ERROR_INVALID_ARGS;
    const auto& desc = texture->getDesc();
    const GpuFormat format = getGpuFormat(desc.format);
    if (format == GPU_FORMAT_UNDEFINED) return GPU_ERROR_NOT_SUPPORTED;
    return getFootprint(format, desc.size.width, desc.size.height,
                        desc.size.depth, desc.mipCount, mipLevel, outFootprint);
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
    if (srcMip >= texDesc.mipCount || srcSlice >= texDesc.arrayLength) {
        return GPU_ERROR_INVALID_ARGS;
    }
    const uint32_t width = std::max(1u, sz.width >> srcMip);
    const uint32_t height = std::max(1u, sz.height >> srcMip);
    const uint32_t depth = std::max(1u, sz.depth >> srcMip);

    // Compute proper row pitch with alignment
    uint32_t bpp = getFormatBytesPerPixel(texDesc.format);
    uint32_t rowPitch = alignTo(width * bpp, GPU_MIN_ROW_PITCH_ALIGNMENT);
    uint32_t slicePitch = rowPitch * height;
    uint64_t totalSize = (uint64_t)slicePitch * depth;
    if (dstOffset > rhiDst->getDesc().size ||
        totalSize > rhiDst->getDesc().size - dstOffset) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Insert resource barriers: transition texture to CopySource, buffer to CopyDestination
    const GpuResourceState originalState = encoder->device->textureStates[src.index];
    gpuCmdSetTextureState(encoder->device, encoder, src, GPU_RESOURCE_STATE_COPY_SOURCE);
    encoder->rhiEncoder->setBufferState(rhiDst, rhi::ResourceState::CopyDestination);
    encoder->rhiEncoder->globalBarrier();

    // Copy with proper row pitch
    encoder->rhiEncoder->copyTextureToBuffer(
        rhiDst, dstOffset, totalSize, rowPitch,
        rhiSrc, srcSlice, srcMip,
        {0, 0, 0},
        {width, height, depth}
    );

    // Restore the caller-visible resource state.
    gpuCmdSetTextureState(encoder->device, encoder, src, originalState);
    encoder->rhiEncoder->globalBarrier();

    return GPU_SUCCESS;
}

GpuResult gpuGetTextureReadbackFootprint(
    GpuDevice device,
    GpuTextureHandle texture,
    uint32_t mipLevel,
    GpuTextureFootprint* outFootprint)
{
    if (!device || !gpuHandleIsValid(texture) || !outFootprint) {
        return GPU_ERROR_INVALID_ARGS;
    }
    rhi::ITexture* rhiTexture =
        device->texturePool.resolve(texture.index, texture.generation);
    return getTextureFootprint(rhiTexture, mipLevel, outFootprint);
}

GpuResult gpuGetTextureDescReadbackFootprint(
    const GpuTextureDesc* texture,
    uint32_t mipLevel,
    GpuTextureFootprint* outFootprint)
{
    if (!texture) return GPU_ERROR_INVALID_ARGS;
    return getFootprint(texture->format, texture->width, texture->height,
                        texture->depth, texture->mipCount, mipLevel, outFootprint);
}

GpuResult gpuGetSurfaceTextureReadbackFootprint(
    GpuSurfaceTexture texture,
    uint32_t mipLevel,
    GpuTextureFootprint* outFootprint)
{
    if (!texture) return GPU_ERROR_INVALID_ARGS;
    return getTextureFootprint(texture->rhiTexture, mipLevel, outFootprint);
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
    GpuTextureFootprint footprint = {};
    return gpuGetTextureReadbackFootprint(device, texture, 0, &footprint) == GPU_SUCCESS
        ? footprint.rowPitch
        : 0;
}
