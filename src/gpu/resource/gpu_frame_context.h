#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_device.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuFrameContext_t* GpuFrameContext;
typedef struct GpuFence_t* GpuFence;
typedef struct GpuCommandQueue_t* GpuCommandQueue;

GpuResult gpuFrameContextCreate(GpuDevice device, uint32_t maxInFlight, GpuFrameContext* outCtx);
void gpuFrameContextDestroy(GpuFrameContext ctx);

GpuResult gpuFrameBegin(GpuFrameContext ctx);
GpuResult gpuFrameEnd(GpuFrameContext ctx, GpuCommandQueue queue);

uint64_t gpuFrameGetIndex(GpuFrameContext ctx);
uint64_t gpuFrameGetCompletedFenceValue(GpuFrameContext ctx);
GpuFence gpuFrameGetFence(GpuFrameContext ctx);

void gpuFrameDeferDestroyBuffer(GpuFrameContext ctx, GpuBufferHandle handle);
void gpuFrameDeferDestroyTexture(GpuFrameContext ctx, GpuTextureHandle handle);
void gpuFrameDeferDestroyTextureView(GpuFrameContext ctx, GpuTextureHandle viewHandle);

GpuResult gpuFrameUploadData(GpuFrameContext ctx, GpuBufferHandle dst, uint64_t offset, uint64_t size, const void* data);
GpuResult gpuFrameRequestReadback(GpuFrameContext ctx, GpuBufferHandle src, uint64_t offset, uint64_t size, void** outMappedPtr);

uint32_t gpuFrameGetInFlightCount(GpuFrameContext ctx);

void gpuSetFrameContext(GpuDevice device, GpuFrameContext ctx);
GpuFrameContext gpuGetFrameContext(GpuDevice device);

#ifdef __cplusplus
}
#endif
