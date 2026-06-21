#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandEncoder_t* GpuCommandEncoder;

typedef enum {
    GPU_RESOURCE_STATE_UNDEFINED = 0,
    GPU_RESOURCE_STATE_COMMON,
    GPU_RESOURCE_STATE_VERTEX_BUFFER,
    GPU_RESOURCE_STATE_INDEX_BUFFER,
    GPU_RESOURCE_STATE_CONSTANT_BUFFER,
    GPU_RESOURCE_STATE_SHADER_RESOURCE,
    GPU_RESOURCE_STATE_UNORDERED_ACCESS,
    GPU_RESOURCE_STATE_RENDER_TARGET,
    GPU_RESOURCE_STATE_DEPTH_WRITE,
    GPU_RESOURCE_STATE_DEPTH_READ,
    GPU_RESOURCE_STATE_PRESENT,
    GPU_RESOURCE_STATE_COPY_SOURCE,
    GPU_RESOURCE_STATE_COPY_DEST,
    GPU_RESOURCE_STATE_STORAGE_BUFFER,
} GpuResourceState;

void gpuCmdSetBufferState(GpuDevice device, GpuCommandEncoder encoder, GpuBufferHandle buffer, GpuResourceState state);
void gpuCmdSetTextureState(GpuDevice device, GpuCommandEncoder encoder, GpuTextureHandle texture, GpuResourceState state);
void gpuCmdGlobalBarrier(GpuCommandEncoder encoder);
GpuResourceState gpuGetBufferState(GpuDevice device, GpuBufferHandle buffer);
GpuResourceState gpuGetTextureState(GpuDevice device, GpuTextureHandle texture);

#ifdef __cplusplus
}
#endif
