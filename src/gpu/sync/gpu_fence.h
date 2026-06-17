#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuFence_t* GpuFence;

GpuResult gpuCreateFence(GpuDevice device, uint64_t initialValue, GpuFence* outFence);
uint64_t gpuFenceGetCurrentValue(GpuFence fence);
GpuResult gpuFenceWait(GpuDevice device, GpuFence fence, uint64_t value, uint32_t timeoutMs);
void gpuDestroyFence(GpuDevice device, GpuFence fence);

typedef struct GpuCommandQueue_t* GpuCommandQueue;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;

GpuResult gpuQueueSubmitWithFence(GpuCommandQueue queue,
                                   uint32_t cmdCount,
                                   GpuCommandBuffer* cmds,
                                   GpuFence signalFence,
                                   uint64_t signalValue);

#ifdef __cplusplus
}
#endif
