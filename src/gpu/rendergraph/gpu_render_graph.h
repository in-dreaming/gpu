#pragma once

#include <stddef.h>
#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/resource/gpu_barrier.h"
#include "gpu/resource/gpu_hazard.h"
#include "gpu/platform/gpu_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuGraph_t* GpuGraph;
typedef uint32_t GpuGraphResource;
typedef struct GpuGraphPass_t* GpuGraphPass;

#define GPU_GRAPH_NULL_RESOURCE 0u

typedef enum {
    GPU_GRAPH_RESOURCE_TEXTURE = 0,
    GPU_GRAPH_RESOURCE_BUFFER  = 1,
} GpuGraphResourceKind;

typedef enum {
    GPU_GRAPH_PASS_RENDER = 0,
    GPU_GRAPH_PASS_COMPUTE = 1,
    GPU_GRAPH_PASS_COPY = 2,
} GpuGraphPassKind;

typedef enum {
    GPU_GRAPH_ACCESS_READ = 0,
    GPU_GRAPH_ACCESS_WRITE = 1,
    GPU_GRAPH_ACCESS_READ_WRITE = 2,
    GPU_GRAPH_ACCESS_PRESENT = 3,
} GpuGraphAccess;

typedef struct GpuGraphPassContext {
    GpuCommandEncoder encoder;
    GpuRenderPassEncoder renderPass;
    GpuComputePassEncoder computePass;
} GpuGraphPassContext;

typedef void (*GpuGraphPassCallback)(GpuGraphPassContext* ctx, void* userData);

typedef enum {
    GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER = 0,
    GPU_GRAPH_EXECUTE_PASS_SUBMIT = 1,
    GPU_GRAPH_EXECUTE_MULTI_QUEUE = 2,
} GpuGraphExecuteMode;

GpuResult gpuGraphCreate(GpuDevice device, GpuGraph* outGraph);
void gpuGraphDestroy(GpuGraph graph);
void gpuGraphReset(GpuGraph graph);

GpuGraphResource gpuGraphCreateTexture(GpuGraph graph, const GpuTextureDesc* desc, const char* name);
GpuGraphResource gpuGraphCreateBuffer(GpuGraph graph, const GpuBufferDesc* desc, const char* name);

GpuGraphResource gpuGraphImportTexture(GpuGraph graph, GpuTextureHandle handle, GpuResourceState initialState, const char* name);
GpuGraphResource gpuGraphImportBuffer(GpuGraph graph, GpuBufferHandle handle, GpuResourceState initialState, const char* name);
GpuGraphResource gpuGraphImportSurfaceTexture(GpuGraph graph, GpuSurfaceTexture surfaceTex, const char* name);

GpuGraphPass gpuGraphAddRenderPass(GpuGraph graph, const char* name);
GpuGraphPass gpuGraphAddComputePass(GpuGraph graph, const char* name);
GpuGraphPass gpuGraphAddCopyPass(GpuGraph graph, const char* name);

void gpuGraphPassRead(GpuGraphPass pass, GpuGraphResource resource);
void gpuGraphPassWrite(GpuGraphPass pass, GpuGraphResource resource);
void gpuGraphPassReadWrite(GpuGraphPass pass, GpuGraphResource resource);
void gpuGraphPassReadSubresource(GpuGraphPass pass, GpuGraphResource resource,
                                 uint32_t mipLevel, uint32_t arrayLayer);
void gpuGraphPassWriteSubresource(GpuGraphPass pass, GpuGraphResource resource,
                                  uint32_t mipLevel, uint32_t arrayLayer);
void gpuGraphPassPresent(GpuGraphPass pass, GpuGraphResource resource);

typedef struct {
    GpuGraphResource resource;
    GpuLoadOp loadOp;
    GpuStoreOp storeOp;
    float clearColor[4];
} GpuGraphColorAttachment;

typedef struct {
    GpuGraphResource resource;
    GpuLoadOp loadOp;
    GpuStoreOp storeOp;
    float clearDepth;
    uint8_t clearStencil;
    GpuTextureHandle depthViewOverride; // optional; when set, binds this DSV instead of the texture default
} GpuGraphDepthAttachment;

void gpuGraphPassSetColorAttachments(GpuGraphPass pass, uint32_t count, const GpuGraphColorAttachment* attachments);
void gpuGraphPassSetDepthAttachment(GpuGraphPass pass, const GpuGraphDepthAttachment* attachment);

void gpuGraphPassSetCallback(GpuGraphPass pass, GpuGraphPassCallback callback, void* userData);

void gpuGraphSetExecuteMode(GpuGraph graph, GpuGraphExecuteMode mode);
GpuGraphExecuteMode gpuGraphGetExecuteMode(GpuGraph graph);
void gpuGraphSetPassProfiling(GpuGraph graph, bool enabled);
bool gpuGraphGetPassProfiling(GpuGraph graph);

GpuResult gpuGraphCompile(GpuGraph graph);
/* queue is used for SINGLE_COMMAND_BUFFER and PASS_SUBMIT; ignored for MULTI_QUEUE (queues resolved internally). */
GpuResult gpuGraphExecute(GpuGraph graph, GpuCommandQueue queue);

GpuTextureHandle gpuGraphGetTexture(GpuGraph graph, GpuGraphResource resource);
GpuBufferHandle gpuGraphGetBuffer(GpuGraph graph, GpuGraphResource resource);

GpuGraphPassKind gpuGraphGetPassKind(GpuGraph graph, uint32_t passIndex);
const char* gpuGraphGetPassName(GpuGraph graph, uint32_t passIndex);
uint32_t gpuGraphGetPassCount(GpuGraph graph);
uint32_t gpuGraphGetExecutionOrderCount(GpuGraph graph);
uint32_t gpuGraphGetExecutionOrderPassIndex(GpuGraph graph, uint32_t sortedIndex);
bool gpuGraphIsPassCulled(GpuGraph graph, uint32_t passIndex);

typedef struct {
    const char* resourceName;
    GpuResourceState before;
    GpuResourceState after;
    GpuAccessFlags access;
    uint32_t srcPassIndex;
    uint32_t destPassIndex;
    uint32_t mipLevel;
    uint32_t mipCount;
    uint32_t arrayLayer;
    uint32_t arrayCount;
    bool isGlobalBarrier;
    GpuHazardKind hazardKind;
    GpuQueueType srcQueue;
    GpuQueueType destQueue;
    bool queueOwnershipTransfer;
} GpuGraphBarrierInfo;

uint32_t gpuGraphGetPassBarrierCount(GpuGraph graph, uint32_t passIndex);
GpuResult gpuGraphGetPassBarrier(GpuGraph graph, uint32_t passIndex, uint32_t barrierIndex,
                                 GpuGraphBarrierInfo* outInfo);

typedef struct {
    uint32_t firstUsePass;
    uint32_t lastUsePass;
    uint32_t allocationId;
    bool aliased;
    bool imported;
} GpuGraphResourceLifetimeInfo;

GpuResult gpuGraphGetResourceLifetime(GpuGraph graph, GpuGraphResource resource,
                                      GpuGraphResourceLifetimeInfo* outInfo);
GpuGraphExecuteMode gpuGraphGetEffectiveExecuteMode(GpuGraph graph);

typedef struct {
    uint32_t allocationId;
    GpuGraphResourceKind kind;
    uint32_t resourceCount;
    uint32_t firstUsePass;
    uint32_t lastUsePass;
    uint64_t bufferSize;
    uint32_t textureWidth;
    uint32_t textureHeight;
    GpuFormat textureFormat;
    uint32_t poolIndex;
    bool aliased;
} GpuGraphTransientAllocationInfo;

uint32_t gpuGraphGetTransientAllocationCount(GpuGraph graph);
GpuResult gpuGraphGetTransientAllocation(GpuGraph graph, uint32_t allocationIndex,
                                           GpuGraphTransientAllocationInfo* outInfo);

uint32_t gpuGraphGetBarrierCount(GpuGraph graph);
GpuResult gpuGraphGetBarrier(GpuGraph graph, uint32_t barrierIndex, GpuGraphBarrierInfo* outInfo);

GpuResult gpuGraphGetPassGpuDurationMs(GpuGraph graph, uint32_t passIndex, float* outDurationMs);

bool gpuDeviceSupportsIndependentQueues(GpuDevice device);

uint32_t gpuGraphGetValidationWarningCount(GpuGraph graph);
const char* gpuGraphGetValidationWarning(GpuGraph graph, uint32_t warningIndex);

GpuResult gpuGraphExportDot(GpuGraph graph, const char* path);
GpuResult gpuGraphExportJson(GpuGraph graph, const char* path);
GpuResult gpuGraphExportDotString(GpuGraph graph, char* outBuffer, size_t* inOutSize);
GpuResult gpuGraphExportJsonString(GpuGraph graph, char* outBuffer, size_t* inOutSize);

uint32_t gpuGetTransientTexturePoolCount(GpuDevice device);
uint32_t gpuGetTransientBufferPoolCount(GpuDevice device);

typedef struct {
    uint32_t allocationId;
    GpuGraphResourceKind kind;
    uint32_t resourceCount;
    uint32_t firstUsePass;
    uint32_t lastUsePass;
    uint64_t sizeBytes;
    uint32_t poolIndex;
    bool objectAliased;
    bool heapPlaced;
} GpuTransientAllocationPlan;

uint32_t gpuGraphBuildTransientAllocationPlan(GpuGraph graph, GpuTransientAllocationPlan* outPlans,
                                              uint32_t maxPlans);

#ifdef __cplusplus
}
#endif
