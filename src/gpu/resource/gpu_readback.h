#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandEncoder_t* GpuCommandEncoder;

GpuResult gpuCreateReadbackBuffer(GpuDevice device, uint64_t size, GpuBufferHandle* outHandle);
GpuResult gpuCmdCopyTextureToBuffer(GpuCommandEncoder encoder,
                                     GpuTextureHandle src,
                                     uint32_t srcMip, uint32_t srcSlice,
                                     GpuBufferHandle dst,
                                     uint64_t dstOffset);
GpuResult gpuMapReadbackBuffer(GpuDevice device, GpuBufferHandle handle, void** outPtr);
void gpuUnmapReadbackBuffer(GpuDevice device, GpuBufferHandle handle);

// Get the row pitch (in bytes) for readback data of a given texture
// Use this to compute pixel offsets when reading mapped readback data
uint32_t gpuGetReadbackRowPitch(GpuTextureHandle texture, GpuDevice device);

#ifdef __cplusplus
}
#endif
