#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandEncoder_t* GpuCommandEncoder;

typedef struct {
    GpuFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t rowPitch;
    uint32_t slicePitch;
    uint64_t totalSize;
} GpuTextureFootprint;

GpuResult gpuCreateReadbackBuffer(GpuDevice device, uint64_t size, GpuBufferHandle* outHandle);
GpuResult gpuCmdCopyTextureToBuffer(GpuCommandEncoder encoder,
                                     GpuTextureHandle src,
                                     uint32_t srcMip, uint32_t srcSlice,
                                     GpuBufferHandle dst,
                                     uint64_t dstOffset);
GpuResult gpuMapReadbackBuffer(GpuDevice device, GpuBufferHandle handle, void** outPtr);
void gpuUnmapReadbackBuffer(GpuDevice device, GpuBufferHandle handle);
GpuResult gpuGetTextureReadbackFootprint(
    GpuDevice device,
    GpuTextureHandle texture,
    uint32_t mipLevel,
    GpuTextureFootprint* outFootprint);

// Get the row pitch (in bytes) for readback data of a given texture
// Use this to compute pixel offsets when reading mapped readback data
uint32_t gpuGetReadbackRowPitch(GpuTextureHandle texture, GpuDevice device);

#ifdef __cplusplus
}
#endif
