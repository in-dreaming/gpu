#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandQueue_t* GpuCommandQueue;
typedef struct GpuCommandEncoder_t* GpuCommandEncoder;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;

typedef enum {
    GPU_QUEUE_TYPE_GRAPHICS = 0,
    GPU_QUEUE_TYPE_COMPUTE  = 1,
    GPU_QUEUE_TYPE_TRANSFER = 2,
} GpuQueueType;

typedef enum {
    GPU_QUEUE_SUPPORT_UNAVAILABLE = 0,
    GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS = 1,
    GPU_QUEUE_SUPPORT_DEDICATED = 2,
} GpuQueueSupport;

typedef struct {
    GpuQueueType type;
    GpuQueueSupport support;
    uint32_t familyIndex;
    const char* reason;
} GpuQueueInfo;

GpuResult gpuGetQueue(GpuDevice device, GpuQueueType type, GpuCommandQueue* outQueue);
GpuResult gpuGetQueueInfo(GpuDevice device, GpuQueueType type, GpuQueueInfo* outInfo);

GpuCommandEncoder gpuBeginCommandEncoder(GpuDevice device, GpuCommandQueue queue);
GpuCommandBuffer gpuFinishCommandEncoder(GpuCommandEncoder encoder);
void gpuCancelCommandEncoder(GpuCommandEncoder encoder);
void gpuDestroyCommandBuffer(GpuCommandBuffer commands);

GpuResult gpuQueueSubmit(GpuCommandQueue queue, uint32_t count, GpuCommandBuffer* cmdBuffers);
GpuResult gpuQueueWaitOnHost(GpuCommandQueue queue);

typedef enum {
    GPU_LOAD_OP_LOAD      = 0,
    GPU_LOAD_OP_CLEAR     = 1,
    GPU_LOAD_OP_DONT_CARE = 2,
} GpuLoadOp;

typedef enum {
    GPU_STORE_OP_STORE     = 0,
    GPU_STORE_OP_DONT_CARE = 1,
} GpuStoreOp;

typedef struct GpuSurfaceTexture_t* GpuSurfaceTexture;
typedef struct GpuRenderPassEncoder_t* GpuRenderPassEncoder;

typedef struct {
    // Either attachment (surface), textureHandle (texture), or viewHandle (texture view) must be set
    GpuSurfaceTexture attachment;
    GpuTextureHandle textureHandle;   // Use for offscreen render-to-texture (legacy)
    GpuTextureHandle viewHandle;      // Use for offscreen render-to-texture with explicit view
    uint32_t mipLevel;               // Mip level for textureHandle rendering
    GpuLoadOp loadOp;
    GpuStoreOp storeOp;
    float clearValue[4];
} GpuRenderPassColorAttachment;

typedef struct {
    GpuTextureHandle textureHandle;   // depth-stencil texture
    GpuTextureHandle viewHandle;      // depth-stencil texture view (preferred)
    GpuLoadOp depthLoadOp;
    GpuStoreOp depthStoreOp;
    float clearDepth;
    GpuLoadOp stencilLoadOp;
    GpuStoreOp stencilStoreOp;
    uint8_t clearStencil;
} GpuRenderPassDepthAttachment;

typedef struct {
    uint32_t colorAttachmentCount;
    GpuRenderPassColorAttachment* colorAttachments;
    const GpuRenderPassDepthAttachment* depthAttachment;  // optional, NULL = no depth
} GpuRenderPassDesc;

GpuRenderPassEncoder gpuCmdBeginRenderPass(GpuCommandEncoder encoder, const GpuRenderPassDesc* desc);
void gpuCmdEndRenderPass(GpuRenderPassEncoder pass);

typedef struct GpuRenderPipeline_t* GpuRenderPipeline;

typedef enum {
    GPU_BUFFER_BINDING_READ = 0,
    GPU_BUFFER_BINDING_READ_WRITE = 1,
    GPU_BUFFER_BINDING_CONSTANT = 2,
} GpuBufferBindingAccess;

void gpuCmdBindRenderPipeline(GpuRenderPassEncoder pass, GpuRenderPipeline pipeline);
void gpuCmdSetViewport(GpuRenderPassEncoder pass, float x, float y, float width, float height);
void gpuCmdSetVertexBuffer(GpuRenderPassEncoder pass, uint32_t slot, GpuBufferHandle buffer, uint64_t offset);
GpuResult gpuCmdBindGraphicsPipeline(GpuRenderPassEncoder pass, GpuPipelineHandle pipeline);
GpuResult gpuCmdSetIndexBuffer(GpuRenderPassEncoder pass, GpuBufferHandle buffer, uint64_t offset, uint8_t index32);
GpuResult gpuCmdSetBindingData(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding, const void* data, size_t size);
GpuResult gpuCmdSetBindingBuffer(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding,
                                 GpuBufferHandle buffer, uint64_t offset, uint64_t range,
                                 GpuBufferBindingAccess access);
GpuResult gpuCmdSetBindingTexture(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding, GpuTextureHandle texture);
GpuResult gpuCmdSetBindingSampler(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding, GpuHandle sampler);
void gpuCmdDraw(GpuRenderPassEncoder pass, uint32_t vertexCount, uint32_t instanceCount,
                uint32_t startVertex, uint32_t startInstance);
void gpuCmdDrawIndexed(GpuRenderPassEncoder pass, uint32_t indexCount, uint32_t instanceCount,
                       uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
GpuResult gpuCmdDrawIndexedIndirectPass(
    GpuRenderPassEncoder pass,
    GpuBufferHandle argumentBuffer,
    uint64_t argumentOffset,
    uint32_t maxDrawCount,
    GpuBufferHandle countBuffer,
    uint64_t countOffset);

typedef struct GpuComputePipeline_t* GpuComputePipeline;
typedef struct GpuComputePassEncoder_t* GpuComputePassEncoder;

GpuComputePassEncoder gpuCmdBeginComputePass(GpuCommandEncoder encoder);
void gpuCmdEndComputePass(GpuComputePassEncoder pass);
void gpuCmdBindComputePipeline(GpuComputePassEncoder pass, GpuComputePipeline pipeline);
GpuResult gpuCmdBindComputePipelineHandle(GpuComputePassEncoder pass, GpuPipelineHandle pipeline);
GpuResult gpuCmdSetComputeBindingData(GpuComputePassEncoder pass, uint32_t set, uint32_t binding,
                                      const void* data, size_t size);
GpuResult gpuCmdSetComputeBindingBuffer(GpuComputePassEncoder pass, uint32_t set, uint32_t binding,
                                        GpuBufferHandle buffer, uint64_t offset, uint64_t range,
                                        GpuBufferBindingAccess access);
GpuResult gpuCmdSetComputeBindingTexture(GpuComputePassEncoder pass, uint32_t set, uint32_t binding,
                                         GpuTextureHandle texture);
GpuResult gpuCmdSetComputeBindingSampler(GpuComputePassEncoder pass, uint32_t set, uint32_t binding,
                                         GpuHandle sampler);
void gpuCmdDispatchCompute(GpuComputePassEncoder pass, uint32_t x, uint32_t y, uint32_t z);
GpuResult gpuCmdDispatchComputeIndirect(
    GpuComputePassEncoder pass,
    GpuBufferHandle argumentBuffer,
    uint64_t argumentOffset);

#ifdef __cplusplus
}
#endif
