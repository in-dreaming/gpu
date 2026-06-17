#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuShaderCompiler_t* GpuShaderCompiler;
typedef struct GpuSlangModule_t* GpuSlangModule;

GpuResult gpuLoadSlangModule(GpuShaderCompiler compiler, const char* modulePath, GpuSlangModule* outModule);
const char* gpuGetModuleEntryPoint(GpuSlangModule module, uint32_t index);
uint32_t gpuGetModuleEntryPointCount(GpuSlangModule module);
void gpuDestroySlangModule(GpuSlangModule module);

#ifdef __cplusplus
}
#endif
