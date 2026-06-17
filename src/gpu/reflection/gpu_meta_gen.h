#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/reflection/gpu_type_info.h"

#ifdef __cplusplus
extern "C" {
#endif

GpuResult gpuGenerateTypeScript(GpuTypeInfo* typeInfo, const char* outputPath);
GpuResult gpuGenerateJSON(GpuTypeInfo* typeInfo, const char* outputPath);

#ifdef __cplusplus
}
#endif
