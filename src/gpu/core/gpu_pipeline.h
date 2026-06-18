#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuShaderProgram_t* GpuShaderProgram;

typedef struct GpuRenderPipeline_t* GpuRenderPipeline;

typedef enum {
    GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST = 0,
    GPU_PRIMITIVE_TOPOLOGY_LINE_LIST     = 1,
    GPU_PRIMITIVE_TOPOLOGY_POINT_LIST    = 2,
    GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP = 3,
    GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP    = 4,
} GpuPrimitiveTopology;

typedef struct {
    GpuFormat format;
} GpuColorTargetDesc;

typedef struct {
    GpuShaderProgram program;
    GpuPrimitiveTopology primitiveTopology;
    GpuColorTargetDesc* targets;
    uint32_t targetCount;
    const char* label;
} GpuRenderPipelineDesc;

GpuResult gpuCreateRenderPipeline(GpuDevice device, const GpuRenderPipelineDesc* desc, GpuRenderPipeline* outPipeline);
void gpuDestroyRenderPipeline(GpuDevice device, GpuRenderPipeline pipeline);

typedef struct GpuComputePipeline_t* GpuComputePipeline;

typedef struct {
    GpuShaderProgram program;
    const char* label;
} GpuComputePipelineDesc;

GpuResult gpuCreateComputePipeline(GpuDevice device, const GpuComputePipelineDesc* desc, GpuComputePipeline* outPipeline);
void gpuDestroyComputePipeline(GpuDevice device, GpuComputePipeline pipeline);

#ifdef __cplusplus
}
#endif
