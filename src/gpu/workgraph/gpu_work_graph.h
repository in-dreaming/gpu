#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/shader/gpu_shader_compiler.h"  // For GpuShaderBinary

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* name;
    GpuShaderBinary nodeShader;
    const char* entryPoint;
    bool isBroadcastNode;
} GpuWorkGraphNodeDesc;

typedef struct {
    uint32_t from;
    uint32_t to;
} GpuWorkGraphEdge;

typedef struct {
    uint32_t nodeCount;
    const GpuWorkGraphNodeDesc* nodes;
    uint32_t edgeCount;
    GpuWorkGraphEdge* edges;
} GpuWorkGraphDesc;

typedef struct GpuWorkGraph_t* GpuWorkGraph;

GpuResult gpuCreateWorkGraph(GpuDevice device, const GpuWorkGraphDesc* desc, GpuWorkGraph* outGraph);
GpuResult gpuWorkGraphInitialize(GpuWorkGraph graph, GpuCommandBuffer cmd);
void gpuCmdDispatchWorkGraph(GpuCommandBuffer cmd, GpuWorkGraph graph, uint32_t recordCount);
void gpuDestroyWorkGraph(GpuDevice device, GpuWorkGraph graph);

#ifdef __cplusplus
}
#endif
