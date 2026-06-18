#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/pipeline/gpu_pipeline_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;
typedef struct GpuRenderPassEncoder_t* GpuRenderPassEncoder;
typedef struct GpuComputePassEncoder_t* GpuComputePassEncoder;

// ============================================================================
// Pipeline Binding
// ============================================================================

// Bind a pipeline (graphics or compute) to the command buffer
// Works for both render pass and compute pass contexts
void gpuCmdBindPipeline(GpuCommandBuffer cmd, GpuPipelineHandle pipeline);

// ============================================================================
// Vertex and Index Buffer Binding
// ============================================================================

typedef enum {
    GPU_INDEX_TYPE_UINT16 = 0,
    GPU_INDEX_TYPE_UINT32 = 1,
} GpuIndexType;

// Bind a vertex buffer to the specified slot
void gpuCmdBindVertexBuffer(GpuCommandBuffer cmd, uint32_t slot, GpuBufferHandle buffer, uint64_t offset);

// Bind multiple vertex buffers at once
void gpuCmdBindVertexBuffers(GpuCommandBuffer cmd, 
                              uint32_t startSlot, 
                              uint32_t count, 
                              const GpuBufferHandle* buffers, 
                              const uint64_t* offsets);

// Bind an index buffer for indexed drawing
void gpuCmdBindIndexBuffer(GpuCommandBuffer cmd, GpuBufferHandle buffer, uint64_t offset, GpuIndexType type);

// ============================================================================
// Draw Commands (Command Buffer Level - for Deferred/Lazy Binding)
// ============================================================================
// Note: For immediate rendering, use GpuRenderPassEncoder draw commands from gpu_command.h

// Draw non-indexed primitives (deferred to render pass)
void gpuCmdDrawDeferred(GpuCommandBuffer cmd, 
                         uint32_t vertexCount, 
                         uint32_t instanceCount, 
                         uint32_t firstVertex, 
                         uint32_t firstInstance);

// Draw indexed primitives (deferred to render pass)
void gpuCmdDrawIndexedDeferred(GpuCommandBuffer cmd, 
                                uint32_t indexCount, 
                                uint32_t instanceCount, 
                                uint32_t firstIndex, 
                                int32_t vertexOffset, 
                                uint32_t firstInstance);

// Draw indirect (vertex data indexed from GPU buffer)
void gpuCmdDrawIndirect(GpuCommandBuffer cmd, 
                         GpuBufferHandle indirectBuffer, 
                         uint64_t offset);

// Draw indexed indirect
void gpuCmdDrawIndexedIndirect(GpuCommandBuffer cmd, 
                                GpuBufferHandle indirectBuffer, 
                                uint64_t offset);

// ============================================================================
// Compute Dispatch
// ============================================================================

// Dispatch compute shader
void gpuCmdDispatch(GpuCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

// Dispatch compute with indirect parameters from buffer
void gpuCmdDispatchIndirect(GpuCommandBuffer cmd, GpuBufferHandle indirectBuffer, uint64_t offset);

// ============================================================================
// Dynamic State (optional, for pipelines without static state)
// ============================================================================

// Note: Viewport/scissor are set via GpuRenderPassEncoder in the existing API
// void gpuCmdSetViewport(GpuRenderPassEncoder pass, float x, float y, float width, float height);
void gpuCmdSetViewportFull(GpuCommandBuffer cmd, float x, float y, float width, float height, float minDepth, float maxDepth);
void gpuCmdSetScissorFull(GpuCommandBuffer cmd, int32_t x, int32_t y, uint32_t width, uint32_t height);
void gpuCmdSetBlendConstants(GpuCommandBuffer cmd, const float constants[4]);
void gpuCmdSetDepthBias(GpuCommandBuffer cmd, float constantBias, float slopeScaledBias, float clamp);

// ============================================================================
// Resource Binding (for non-bindless usage)
// ============================================================================

// Bind a buffer to a specific binding point
void gpuCmdBindBuffer(GpuCommandBuffer cmd, uint32_t set, uint32_t binding, GpuBufferHandle buffer, uint64_t offset, uint64_t range);

// Bind a texture to a specific binding point
void gpuCmdBindTexture(GpuCommandBuffer cmd, uint32_t set, uint32_t binding, GpuTextureHandle texture);

// Bind a sampler to a specific binding point
void gpuCmdBindSampler(GpuCommandBuffer cmd, uint32_t set, uint32_t binding, uint32_t samplerIndex);

#ifdef __cplusplus
}
#endif
