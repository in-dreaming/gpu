#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_device.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPU_FEATURE_BINDLESS         = 0,
    GPU_FEATURE_MESH_SHADER      = 1,
    GPU_FEATURE_RAY_TRACING      = 2,
    GPU_FEATURE_WORK_GRAPH       = 3,
    GPU_FEATURE_COOPERATIVE_MATRIX = 4,
    GPU_FEATURE_SPARSE_RESOURCE  = 5,
    GPU_FEATURE_ASYNC_COMPUTE    = 6,
    GPU_FEATURE_COUNT,
} GpuFeature;

typedef struct {
    bool supported[GPU_FEATURE_COUNT];
    uint32_t limits[GPU_FEATURE_COUNT];
} GpuFeatureTable;

GpuResult gpuBuildFeatureTable(GpuDevice device, GpuFeatureTable* outTable);
bool gpuIsFeatureSupportedEx(const GpuFeatureTable* table, GpuFeature feature);
uint32_t gpuGetFeatureLimit(const GpuFeatureTable* table, GpuFeature feature);
const char* gpuFeatureToString(GpuFeature feature);

GpuResult gpuFeatureTableToJson(const GpuFeatureTable* table, char* outBuffer, size_t bufferSize);
GpuResult gpuFeatureTableFromJson(const char* json, GpuFeatureTable* outTable);

#ifdef __cplusplus
}
#endif
