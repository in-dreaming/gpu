#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_device.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPU_FEATURE_BINDLESS            = 0,
    GPU_FEATURE_MESH_SHADER         = 1,
    GPU_FEATURE_RAY_TRACING         = 2,
    GPU_FEATURE_WORK_GRAPH          = 3,
    GPU_FEATURE_COOPERATIVE_MATRIX  = 4,
    GPU_FEATURE_SPARSE_RESOURCE     = 5,
    GPU_FEATURE_ASYNC_COMPUTE       = 6,
    GPU_FEATURE_TIMELINE_FENCE      = 7,
    GPU_FEATURE_DESCRIPTOR_INDEXING = 8,
    GPU_FEATURE_MULTI_QUEUE         = 9,
    GPU_FEATURE_PRESENT_MAILBOX     = 10,
    GPU_FEATURE_PRESENT_IMMEDIATE   = 11,
    GPU_FEATURE_COUNT,
} GpuFeature;

typedef enum {
    GPU_FEATURE_SUPPORT_UNSUPPORTED = 0,
    GPU_FEATURE_SUPPORT_EMULATED    = 1,
    GPU_FEATURE_SUPPORT_NATIVE      = 2,
} GpuFeatureSupport;

typedef struct {
    GpuFeatureSupport supported[GPU_FEATURE_COUNT];
    uint32_t limits[GPU_FEATURE_COUNT];
    char reasons[GPU_FEATURE_COUNT][128];
} GpuFeatureTable;

GpuResult gpuBuildFeatureTable(GpuDevice device, GpuFeatureTable* outTable);
bool gpuIsFeatureSupportedEx(const GpuFeatureTable* table, GpuFeature feature);
uint32_t gpuGetFeatureLimit(const GpuFeatureTable* table, GpuFeature feature);
const char* gpuFeatureToString(GpuFeature feature);

GpuResult gpuFeatureTableToJson(const GpuFeatureTable* table, char* outBuffer, size_t bufferSize);
GpuResult gpuFeatureTableFromJson(const char* json, GpuFeatureTable* outTable);

typedef struct {
    GpuFeatureSupport support;
    uint32_t limit;
    const char* reason;
} GpuFeatureInfo;

GpuResult gpuGetFeatureInfo(GpuDevice device, GpuFeature feature, GpuFeatureInfo* outInfo);
const char* gpuFeatureSupportToString(GpuFeatureSupport support);

#ifdef __cplusplus
}
#endif
