#include "gpu/capability/gpu_fallback_registry.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include <string.h>

static GpuFallbackRegistry s_fallbackRegistry;

void gpuInitFallbackRegistry(void)
{
    s_fallbackRegistry.count = 0;
    for (int i = 0; i < GPU_MAX_FALLBACK_ENTRIES; i++) {
        s_fallbackRegistry.entries[i].feature = (GpuFeature)0;
        s_fallbackRegistry.entries[i].fallbackShaderPath = NULL;
        s_fallbackRegistry.entries[i].fallbackPipelineType = GPU_PIPELINE_TYPE_GRAPHICS;
    }
}

void gpuCleanupFallbackRegistry(void)
{
    memset(&s_fallbackRegistry, 0, sizeof(s_fallbackRegistry));
}

GpuResult gpuRegisterFallback(GpuDevice device, const GpuFallbackEntry* entry)
{
    (void)device;

    if (!entry) return GPU_ERROR_INVALID_ARGS;
    if (s_fallbackRegistry.count >= GPU_MAX_FALLBACK_ENTRIES) return GPU_ERROR_OUT_OF_MEMORY;

    s_fallbackRegistry.entries[s_fallbackRegistry.count] = *entry;
    s_fallbackRegistry.count++;

    return GPU_SUCCESS;
}

GpuResult gpuResolveFallback(GpuDevice device, GpuFeature requiredFeature,
                              const char** outShaderPath, GpuPipelineType* outType)
{
    (void)device;

    if (outShaderPath) *outShaderPath = NULL;
    if (outType) *outType = GPU_PIPELINE_TYPE_GRAPHICS;

    for (uint32_t i = 0; i < s_fallbackRegistry.count; i++) {
        if (s_fallbackRegistry.entries[i].feature == requiredFeature) {
            if (outShaderPath) *outShaderPath = s_fallbackRegistry.entries[i].fallbackShaderPath;
            if (outType) *outType = s_fallbackRegistry.entries[i].fallbackPipelineType;
            return GPU_SUCCESS;
        }
    }

    return GPU_ERROR_NOT_FOUND;
}
