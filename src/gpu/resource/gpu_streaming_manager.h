#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuStreamingManager_t* GpuStreamingManager;

GpuResult gpuStreamingManagerCreate(GpuDevice device, uint64_t uploadHeapSize, GpuStreamingManager* outMgr);
GpuResult gpuStreamingRequest(GpuStreamingManager mgr, GpuTextureHandle texture,
                              uint32_t mipStart, uint32_t mipCount,
                              const void* data, uint64_t dataSize);
GpuResult gpuStreamingUpdate(GpuStreamingManager mgr);
void gpuStreamingManagerDestroy(GpuStreamingManager mgr);

#ifdef __cplusplus
}
#endif
