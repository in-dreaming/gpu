#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_format.h"
#include "gpu/core/gpu_pipeline.h"
#include "gpu/shader/gpu_shader_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef GpuHandle GpuAccelerationStructureHandle;

typedef struct {
    GpuBufferHandle vertexBuffer;
    GpuBufferHandle indexBuffer;
    uint32_t triangleCount;
    uint32_t vertexStride;
    GpuFormat vertexFormat;
} GpuAccelerationStructureGeometry;

typedef struct {
    uint32_t geometryCount;
    const GpuAccelerationStructureGeometry* geometries;
    bool allowUpdate;
} GpuBottomLevelASDesc;

typedef struct {
    uint32_t instanceCount;
    GpuBufferHandle instanceBuffer;
} GpuTopLevelASDesc;

GpuResult gpuCreateBottomLevelAS(GpuDevice device, const GpuBottomLevelASDesc* desc, GpuAccelerationStructureHandle* outAS);
GpuResult gpuCreateTopLevelAS(GpuDevice device, const GpuTopLevelASDesc* desc, GpuAccelerationStructureHandle* outAS);
GpuResult gpuBuildAccelerationStructure(GpuCommandBuffer cmd, GpuAccelerationStructureHandle as, bool update);
GpuResult gpuDestroyAccelerationStructure(GpuDevice device, GpuAccelerationStructureHandle as);

typedef struct {
    const char* label;
    GpuShaderBinary rayGenShader;
    GpuShaderBinary missShader;
    GpuShaderBinary closestHitShader;
    GpuShaderBinary anyHitShader;
    GpuShaderBinary intersectionShader;
    uint32_t maxRecursion;
    uint32_t maxRayPayloadSize;
} GpuRayTracingPipelineDesc;

GpuResult gpuCreateRayTracingPipeline(GpuDevice device, const GpuRayTracingPipelineDesc* desc, GpuPipelineHandle* outPipeline);
void gpuCmdTraceRays(GpuCommandBuffer cmd, GpuPipelineHandle rtPipeline, uint32_t width, uint32_t height, uint32_t depth);

#ifdef __cplusplus
}
#endif
