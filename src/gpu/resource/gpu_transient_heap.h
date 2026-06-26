#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_buffer.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

bool gpuDeviceSupportsTransientHeap(GpuDevice device);

GpuResult gpuCreateBufferFromHeapAlloc(GpuDevice device, void* heap, void* heapAlloc,
                                       const GpuBufferDesc* desc, GpuBufferHandle* outHandle);
void gpuReleaseHeapAlloc(GpuDevice device, void* heap, void* heapAlloc);

#ifdef __cplusplus
}
#endif
