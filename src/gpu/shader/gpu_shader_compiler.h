#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuShaderCompiler_t* GpuShaderCompiler;

typedef enum {
    GPU_SHADER_TARGET_SPIRV = 0,
    GPU_SHADER_TARGET_DXIL  = 1,
    GPU_SHADER_TARGET_MSL   = 2,
    GPU_SHADER_TARGET_WGSL  = 3,
    GPU_SHADER_TARGET_CUDA  = 4,
} GpuShaderTarget;

typedef struct {
    const char* sourcePath;
    const char* entryPoint;          // Vertex/Compute entry point
    const char* fragmentEntryPoint;  // Optional: fragment entry point for graphics pipelines
    GpuShaderTarget target;
    const char* const* defineNames;
    const char* const* defineValues;
    uint32_t defineCount;
} GpuShaderCompileDesc;

typedef struct {
    const uint8_t* data;
    uint64_t size;
} GpuShaderBinary;

typedef struct GpuShaderProgram_t* GpuShaderProgram;

GpuResult gpuCreateShaderCompiler(GpuDevice device, GpuShaderCompiler* outCompiler);
GpuResult gpuCompileShader(GpuShaderCompiler compiler, const GpuShaderCompileDesc* desc, GpuShaderProgram* outProgram);
void gpuDestroyShaderProgram(GpuShaderProgram program);
void gpuDestroyShaderCompiler(GpuShaderCompiler compiler);

const char* gpuGetShaderCompileDiagnostic(GpuShaderCompiler compiler);
const uint8_t* gpuGetShaderProgramData(GpuShaderProgram program, uint64_t* outSize);

#ifdef __cplusplus
}
#endif
