#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/reflection/gpu_type_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuShaderCompiler_t* GpuShaderCompiler;
typedef struct GpuReflectionCache_t* GpuReflectionCache;

GpuResult gpuReflectionCacheCreate(GpuReflectionCache* outCache);
GpuResult gpuReflectShader(GpuReflectionCache cache, GpuShaderCompiler compiler,
                           const char* modulePath, const char* entryPoint,
                           GpuTypeInfo** outTypeInfo);
void gpuReflectionCacheDestroy(GpuReflectionCache cache);

#ifdef __cplusplus
}
#endif
