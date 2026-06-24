#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/resource/gpu_barrier.h"
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
} GpuGraphAccess;

typedef struct GpuGraphPassContext {
    GpuCommandEncoder encoder;
    GpuRenderPassEncoder renderPass;
    GpuComputePassEncoder computePass;
} GpuGraphPassContext;

typedef void (*GpuGraphPassCallback)(GpuGraphPassContext* ctx, void* userData);

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

GpuResult gpuGraphCompile(GpuGraph graph);
GpuResult gpuGraphExecute(GpuGraph graph, GpuCommandQueue queue);

GpuTextureHandle gpuGraphGetTexture(GpuGraph graph, GpuGraphResource resource);
GpuBufferHandle gpuGraphGetBuffer(GpuGraph graph, GpuGraphResource resource);

GpuGraphPassKind gpuGraphGetPassKind(GpuGraph graph, uint32_t passIndex);
const char* gpuGraphGetPassName(GpuGraph graph, uint32_t passIndex);
uint32_t gpuGraphGetPassCount(GpuGraph graph);

GpuResult gpuGraphExportDot(GpuGraph graph, const char* path);
GpuResult gpuGraphExportJson(GpuGraph graph, const char* path);

#ifdef __cplusplus
}
#endif
