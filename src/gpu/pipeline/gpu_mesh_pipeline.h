#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/capability/gpu_capabilities.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;

// ============================================================================
// Mesh Pipeline
// ============================================================================

// Mesh pipeline description
typedef struct {
    const char* label;
    
    // Mesh shader stages
    GpuShaderBinary amplificationShader;    // Optional: task/amplification shader
    GpuShaderBinary meshShader;             // Required: mesh shader
    GpuShaderBinary fragmentShader;         // Required: fragment shader
    
    // Render targets
    GpuColorTargetState* colorTargets;
    uint32_t colorTargetCount;
    GpuFormat depthStencilFormat;
    uint32_t sampleCount;
} GpuMeshPipelineDesc;

// Create a mesh pipeline
// Requires GpuCapabilities.supportMeshShader == true
GpuResult gpuCreateMeshPipeline(GpuDevice device, const GpuMeshPipelineDesc* desc, GpuPipelineHandle* outPipeline);

// ============================================================================
// Mesh Draw Commands
// ============================================================================

// Dispatch mesh shader tasks
// amplificationShader: number of thread groups to launch
// meshShader: output number of mesh workgroups per amplification task
void gpuCmdDrawMeshTasks(GpuCommandBuffer cmd, 
                          uint32_t groupCountX, 
                          uint32_t groupCountY, 
                          uint32_t groupCountZ);

// Draw mesh tasks with indirect parameters from buffer
void gpuCmdDrawMeshTasksIndirect(GpuCommandBuffer cmd, 
                                  GpuBufferHandle indirectBuffer, 
                                  uint64_t offset);

// Draw mesh tasks with indirect count
void gpuCmdDrawMeshTasksIndirectCount(GpuCommandBuffer cmd,
                                       GpuBufferHandle indirectBuffer,
                                       uint64_t indirectOffset,
                                       GpuBufferHandle countBuffer,
                                       uint64_t countOffset,
                                       uint32_t maxDrawCount);

// ============================================================================
// Mesh Shader Capabilities Check
// ============================================================================

// Check if mesh shaders are supported on this device
static inline bool gpuMeshShaderIsSupported(GpuDevice device) {
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    return caps.supportMeshShader;
}

// Get mesh shader limits
typedef struct {
    uint32_t maxMeshOutputVertices;
    uint32_t maxMeshOutputPrimitives;
    uint32_t maxMeshOutputMemory;
    uint32_t maxMeshWorkGroupSize;
    uint32_t maxTaskWorkGroupSize;
    uint32_t maxTaskPayloadSize;
} GpuMeshShaderLimits;

GpuResult gpuGetMeshShaderLimits(GpuDevice device, GpuMeshShaderLimits* outLimits);

#ifdef __cplusplus
}
#endif
