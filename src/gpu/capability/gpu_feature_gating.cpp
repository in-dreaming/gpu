#include "gpu/capability/gpu_feature_gating.h"
#include "gpu/core/gpu_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

GpuResult gpuBuildFeatureTable(GpuDevice device, GpuFeatureTable* outTable)
{
    if (!device || !outTable) return GPU_ERROR_INVALID_ARGS;

    memset(outTable, 0, sizeof(GpuFeatureTable));

    // Query slang-rhi features directly
    outTable->supported[GPU_FEATURE_BINDLESS] = device->rhiDevice->hasFeature(rhi::Feature::Bindless);
    outTable->supported[GPU_FEATURE_MESH_SHADER] = device->rhiDevice->hasFeature(rhi::Feature::MeshShader);
    outTable->supported[GPU_FEATURE_RAY_TRACING] = device->rhiDevice->hasFeature(rhi::Feature::RayTracing);
    outTable->supported[GPU_FEATURE_COOPERATIVE_MATRIX] = device->rhiDevice->hasFeature(rhi::Feature::CooperativeMatrix);
    
    // Work Graph is framework-level (D3D12 only, not in slang-rhi)
    // For now, set to false - will be detected by Work Graph implementation
    outTable->supported[GPU_FEATURE_WORK_GRAPH] = false;
    
    // Sparse resources - check if supported
    outTable->supported[GPU_FEATURE_SPARSE_RESOURCE] = false; // Not exposed by slang-rhi yet
    
    // Async compute - check if we have a compute queue
    outTable->supported[GPU_FEATURE_ASYNC_COMPUTE] = (device->computeQueue != nullptr);

    // Set limits (simplified - use binary 0/1 for most)
    outTable->limits[GPU_FEATURE_BINDLESS] = outTable->supported[GPU_FEATURE_BINDLESS] ? 1000000 : 0;
    outTable->limits[GPU_FEATURE_MESH_SHADER] = outTable->supported[GPU_FEATURE_MESH_SHADER] ? 1 : 0;
    outTable->limits[GPU_FEATURE_RAY_TRACING] = outTable->supported[GPU_FEATURE_RAY_TRACING] ? 1 : 0;
    outTable->limits[GPU_FEATURE_WORK_GRAPH] = outTable->supported[GPU_FEATURE_WORK_GRAPH] ? 1 : 0;
    outTable->limits[GPU_FEATURE_COOPERATIVE_MATRIX] = outTable->supported[GPU_FEATURE_COOPERATIVE_MATRIX] ? 1 : 0;
    outTable->limits[GPU_FEATURE_SPARSE_RESOURCE] = 0;
    outTable->limits[GPU_FEATURE_ASYNC_COMPUTE] = outTable->supported[GPU_FEATURE_ASYNC_COMPUTE] ? 1 : 0;

    return GPU_SUCCESS;
}

bool gpuIsFeatureSupportedEx(const GpuFeatureTable* table, GpuFeature feature)
{
    if (!table || feature < 0 || feature >= GPU_FEATURE_COUNT) return false;
    return table->supported[feature];
}

uint32_t gpuGetFeatureLimit(const GpuFeatureTable* table, GpuFeature feature)
{
    if (!table || feature < 0 || feature >= GPU_FEATURE_COUNT) return 0;
    return table->limits[feature];
}

const char* gpuFeatureToString(GpuFeature feature)
{
    switch (feature) {
    case GPU_FEATURE_BINDLESS:         return "Bindless";
    case GPU_FEATURE_MESH_SHADER:      return "MeshShader";
    case GPU_FEATURE_RAY_TRACING:      return "RayTracing";
    case GPU_FEATURE_WORK_GRAPH:       return "WorkGraph";
    case GPU_FEATURE_COOPERATIVE_MATRIX: return "CooperativeMatrix";
    case GPU_FEATURE_SPARSE_RESOURCE:  return "SparseResource";
    case GPU_FEATURE_ASYNC_COMPUTE:    return "AsyncCompute";
    default:                           return "Unknown";
    }
}

GpuResult gpuFeatureTableToJson(const GpuFeatureTable* table, char* outBuffer, size_t bufferSize)
{
    if (!table || !outBuffer || bufferSize == 0) return GPU_ERROR_INVALID_ARGS;

    int written = snprintf(outBuffer, bufferSize, "{\n");
    if (written < 0 || (size_t)written >= bufferSize) return GPU_ERROR_INTERNAL;

    size_t offset = (size_t)written;

    for (int i = 0; i < GPU_FEATURE_COUNT; i++) {
        const char* name = gpuFeatureToString((GpuFeature)i);
        int featureWritten = snprintf(outBuffer + offset, bufferSize - offset,
            "  \"%s\": {\"supported\": %s, \"limit\": %u}%s\n",
            name,
            table->supported[i] ? "true" : "false",
            table->limits[i],
            (i < GPU_FEATURE_COUNT - 1) ? "," : "");

        if (featureWritten < 0 || (size_t)featureWritten >= bufferSize - offset)
            return GPU_ERROR_INTERNAL;

        offset += (size_t)featureWritten;
    }

    int closeWritten = snprintf(outBuffer + offset, bufferSize - offset, "}\n");
    if (closeWritten < 0 || (size_t)closeWritten >= bufferSize - offset)
        return GPU_ERROR_INTERNAL;

    return GPU_SUCCESS;
}

GpuResult gpuFeatureTableFromJson(const char* json, GpuFeatureTable* outTable)
{
    if (!json || !outTable) return GPU_ERROR_INVALID_ARGS;

    memset(outTable, 0, sizeof(GpuFeatureTable));

    for (int i = 0; i < GPU_FEATURE_COUNT; i++) {
        const char* name = gpuFeatureToString((GpuFeature)i);
        char searchBuf[64];
        snprintf(searchBuf, sizeof(searchBuf), "\"%s\"", name);

        const char* pos = strstr(json, searchBuf);
        if (pos) {
            const char* supportedPos = strstr(pos, "\"supported\"");
            if (supportedPos) {
                const char* boolPos = strstr(supportedPos, ":");
                if (boolPos) {
                    while (*boolPos && (*boolPos == ':' || *boolPos == ' ')) boolPos++;
                    if (strncmp(boolPos, "true", 4) == 0) {
                        outTable->supported[i] = true;
                    }
                }
            }

            const char* limitPos = strstr(pos, "\"limit\"");
            if (limitPos) {
                const char* numPos = strstr(limitPos, ":");
                if (numPos) {
                    while (*numPos && (*numPos == ':' || *numPos == ' ')) numPos++;
                    outTable->limits[i] = (uint32_t)atoi(numPos);
                }
            }
        }
    }

    return GPU_SUCCESS;
}
