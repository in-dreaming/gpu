#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_device.h"
#include "gpu/capability/gpu_feature_gating.h"
#include "gpu/pipeline/gpu_pipeline_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    GpuFeature feature;
    const char* fallbackShaderPath;
    GpuPipelineType fallbackPipelineType;
} GpuFallbackEntry;

#define GPU_MAX_FALLBACK_ENTRIES 32

typedef struct {
    GpuFallbackEntry entries[GPU_MAX_FALLBACK_ENTRIES];
    uint32_t count;
} GpuFallbackRegistry;

GpuResult gpuRegisterFallback(GpuDevice device, const GpuFallbackEntry* entry);
GpuResult gpuResolveFallback(GpuDevice device, GpuFeature requiredFeature,
                              const char** outShaderPath, GpuPipelineType* outType);

void gpuInitFallbackRegistry(void);
void gpuCleanupFallbackRegistry(void);

#ifdef __cplusplus
}
#endif
