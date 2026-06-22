#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_sampler.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Debug Markers (Phase E)
// ============================================================================

typedef struct {
    float r, g, b;
} GpuMarkerColor;

// Push a debug group on the command encoder (outside pass)
void gpuCmdPushDebugGroup(GpuCommandEncoder encoder, const char* name, GpuMarkerColor color);

// Pop the current debug group on the command encoder
void gpuCmdPopDebugGroup(GpuCommandEncoder encoder);

// Insert a single debug marker on the command encoder
void gpuCmdInsertDebugMarker(GpuCommandEncoder encoder, const char* name, GpuMarkerColor color);

// Push a debug group on a render pass encoder
void gpuCmdPushRenderDebugGroup(GpuRenderPassEncoder pass, const char* name, GpuMarkerColor color);
void gpuCmdPopRenderDebugGroup(GpuRenderPassEncoder pass);
void gpuCmdInsertRenderDebugMarker(GpuRenderPassEncoder pass, const char* name, GpuMarkerColor color);

// Push a debug group on a compute pass encoder
void gpuCmdPushComputeDebugGroup(GpuComputePassEncoder pass, const char* name, GpuMarkerColor color);
void gpuCmdPopComputeDebugGroup(GpuComputePassEncoder pass);
void gpuCmdInsertComputeDebugMarker(GpuComputePassEncoder pass, const char* name, GpuMarkerColor color);

// ============================================================================
// Debug Names (Phase E)
// ============================================================================

typedef struct GpuDevice_t* GpuDevice;

// Set debug name on a buffer
GpuResult gpuSetBufferDebugName(GpuDevice device, GpuBufferHandle buffer, const char* name);
// Set debug name on a texture
GpuResult gpuSetTextureDebugName(GpuDevice device, GpuTextureHandle texture, const char* name);
// Set debug name on a sampler
GpuResult gpuSetSamplerDebugName(GpuDevice device, GpuSamplerHandle sampler, const char* name);

// Get debug name from a buffer (returns the label set at creation or via set)
const char* gpuGetBufferDebugName(GpuDevice device, GpuBufferHandle buffer);
const char* gpuGetTextureDebugName(GpuDevice device, GpuTextureHandle texture);
const char* gpuGetSamplerDebugName(GpuDevice device, GpuSamplerHandle sampler);

#ifdef __cplusplus
}
#endif
