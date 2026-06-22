#include "gpu/capability/gpu_feature_gating.h"
#include "gpu/core/gpu_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

static const char* s_unsupportedReasons[GPU_FEATURE_COUNT] = {
    "Bindless not supported by backend",
    "Mesh shader not supported by backend",
    "Ray tracing not supported by backend",
    "Work graph not supported by backend",
    "Cooperative matrix not supported by backend",
    "Sparse resource not exposed by slang-rhi",
    "Async compute not available (single queue)",
    "Timeline fence not supported by backend",
    "Descriptor indexing not supported by backend",
    "Multi-queue not available (single queue)",
    "Mailbox present mode not supported",
    "Immediate present mode not supported",
};

GpuResult gpuBuildFeatureTable(GpuDevice device, GpuFeatureTable* outTable)
{
    if (!device || !outTable) return GPU_ERROR_INVALID_ARGS;

    memset(outTable, 0, sizeof(GpuFeatureTable));

    bool nativeBindless = device->rhiDevice->hasFeature(rhi::Feature::Bindless);
    outTable->supported[GPU_FEATURE_BINDLESS] = nativeBindless
        ? GPU_FEATURE_SUPPORT_NATIVE : GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_BINDLESS] = nativeBindless ? 1000000 : 0;
    if (!nativeBindless) { strncpy_s(outTable->reasons[GPU_FEATURE_BINDLESS], 128, s_unsupportedReasons[GPU_FEATURE_BINDLESS], 127); }

    bool nativeMesh = device->rhiDevice->hasFeature(rhi::Feature::MeshShader);
    outTable->supported[GPU_FEATURE_MESH_SHADER] = nativeMesh
        ? GPU_FEATURE_SUPPORT_NATIVE : GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_MESH_SHADER] = nativeMesh ? 1 : 0;
    if (!nativeMesh) strncpy(outTable->reasons[GPU_FEATURE_MESH_SHADER], s_unsupportedReasons[GPU_FEATURE_MESH_SHADER], 127);

    bool nativeRT = device->rhiDevice->hasFeature(rhi::Feature::RayTracing);
    outTable->supported[GPU_FEATURE_RAY_TRACING] = nativeRT
        ? GPU_FEATURE_SUPPORT_NATIVE : GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_RAY_TRACING] = nativeRT ? 1 : 0;
    if (!nativeRT) strncpy(outTable->reasons[GPU_FEATURE_RAY_TRACING], s_unsupportedReasons[GPU_FEATURE_RAY_TRACING], 127);

    outTable->supported[GPU_FEATURE_WORK_GRAPH] = GPU_FEATURE_SUPPORT_EMULATED;
    outTable->limits[GPU_FEATURE_WORK_GRAPH] = 1;
    strncpy(outTable->reasons[GPU_FEATURE_WORK_GRAPH], "Emulated via compute dispatch chains", 127);

    bool nativeCoop = device->rhiDevice->hasFeature(rhi::Feature::CooperativeMatrix);
    outTable->supported[GPU_FEATURE_COOPERATIVE_MATRIX] = nativeCoop
        ? GPU_FEATURE_SUPPORT_NATIVE : GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_COOPERATIVE_MATRIX] = nativeCoop ? 1 : 0;
    if (!nativeCoop) strncpy(outTable->reasons[GPU_FEATURE_COOPERATIVE_MATRIX], s_unsupportedReasons[GPU_FEATURE_COOPERATIVE_MATRIX], 127);

    outTable->supported[GPU_FEATURE_SPARSE_RESOURCE] = GPU_FEATURE_SUPPORT_EMULATED;
    outTable->limits[GPU_FEATURE_SPARSE_RESOURCE] = 0;
    strncpy(outTable->reasons[GPU_FEATURE_SPARSE_RESOURCE], "Emulated via tile tracking; no hardware sparse", 127);

    bool hasComputeQueue = (device->computeQueue != nullptr);
    outTable->supported[GPU_FEATURE_ASYNC_COMPUTE] = hasComputeQueue
        ? GPU_FEATURE_SUPPORT_NATIVE : GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_ASYNC_COMPUTE] = hasComputeQueue ? 1 : 0;
    if (!hasComputeQueue) strncpy(outTable->reasons[GPU_FEATURE_ASYNC_COMPUTE], s_unsupportedReasons[GPU_FEATURE_ASYNC_COMPUTE], 127);

    outTable->supported[GPU_FEATURE_TIMELINE_FENCE] = GPU_FEATURE_SUPPORT_EMULATED;
    outTable->limits[GPU_FEATURE_TIMELINE_FENCE] = 1;
    strncpy(outTable->reasons[GPU_FEATURE_TIMELINE_FENCE], "Emulated via CPU polling", 127);

    outTable->supported[GPU_FEATURE_DESCRIPTOR_INDEXING] = GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_DESCRIPTOR_INDEXING] = 0;
    strncpy(outTable->reasons[GPU_FEATURE_DESCRIPTOR_INDEXING], s_unsupportedReasons[GPU_FEATURE_DESCRIPTOR_INDEXING], 127);

    bool hasMultiQueue = (device->graphicsQueue != device->computeQueue || device->graphicsQueue != device->transferQueue);
    outTable->supported[GPU_FEATURE_MULTI_QUEUE] = hasMultiQueue
        ? GPU_FEATURE_SUPPORT_NATIVE : GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_MULTI_QUEUE] = hasMultiQueue ? 3 : 1;
    if (!hasMultiQueue) strncpy(outTable->reasons[GPU_FEATURE_MULTI_QUEUE], s_unsupportedReasons[GPU_FEATURE_MULTI_QUEUE], 127);

    outTable->supported[GPU_FEATURE_PRESENT_MAILBOX] = GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_PRESENT_MAILBOX] = 0;
    strncpy(outTable->reasons[GPU_FEATURE_PRESENT_MAILBOX], s_unsupportedReasons[GPU_FEATURE_PRESENT_MAILBOX], 127);

    outTable->supported[GPU_FEATURE_PRESENT_IMMEDIATE] = GPU_FEATURE_SUPPORT_UNSUPPORTED;
    outTable->limits[GPU_FEATURE_PRESENT_IMMEDIATE] = 0;
    strncpy(outTable->reasons[GPU_FEATURE_PRESENT_IMMEDIATE], s_unsupportedReasons[GPU_FEATURE_PRESENT_IMMEDIATE], 127);

    return GPU_SUCCESS;
}

bool gpuIsFeatureSupportedEx(const GpuFeatureTable* table, GpuFeature feature)
{
    if (!table || (int)feature < 0 || feature >= GPU_FEATURE_COUNT) return false;
    return table->supported[feature] != GPU_FEATURE_SUPPORT_UNSUPPORTED;
}

uint32_t gpuGetFeatureLimit(const GpuFeatureTable* table, GpuFeature feature)
{
    if (!table || (int)feature < 0 || feature >= GPU_FEATURE_COUNT) return 0;
    return table->limits[feature];
}

const char* gpuFeatureToString(GpuFeature feature)
{
    switch (feature) {
    case GPU_FEATURE_BINDLESS:            return "Bindless";
    case GPU_FEATURE_MESH_SHADER:         return "MeshShader";
    case GPU_FEATURE_RAY_TRACING:         return "RayTracing";
    case GPU_FEATURE_WORK_GRAPH:          return "WorkGraph";
    case GPU_FEATURE_COOPERATIVE_MATRIX:  return "CooperativeMatrix";
    case GPU_FEATURE_SPARSE_RESOURCE:     return "SparseResource";
    case GPU_FEATURE_ASYNC_COMPUTE:       return "AsyncCompute";
    case GPU_FEATURE_TIMELINE_FENCE:      return "TimelineFence";
    case GPU_FEATURE_DESCRIPTOR_INDEXING: return "DescriptorIndexing";
    case GPU_FEATURE_MULTI_QUEUE:         return "MultiQueue";
    case GPU_FEATURE_PRESENT_MAILBOX:     return "PresentMailbox";
    case GPU_FEATURE_PRESENT_IMMEDIATE:   return "PresentImmediate";
    default:                              return "Unknown";
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
        const char* supportStr = "unsupported";
        if (table->supported[i] == GPU_FEATURE_SUPPORT_NATIVE) supportStr = "native";
        else if (table->supported[i] == GPU_FEATURE_SUPPORT_EMULATED) supportStr = "emulated";

        int featureWritten = snprintf(outBuffer + offset, bufferSize - offset,
            "  \"%s\": {\"support\": \"%s\", \"limit\": %u, \"reason\": \"%s\"}%s\n",
            name, supportStr, table->limits[i],
            table->reasons[i][0] ? table->reasons[i] : "",
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
            const char* supportPos = strstr(pos, "\"support\"");
            if (supportPos) {
                const char* valPos = strstr(supportPos, ":");
                if (valPos) {
                    while (*valPos && (*valPos == ':' || *valPos == ' ' || *valPos == '"')) valPos++;
                    if (strncmp(valPos, "native", 6) == 0) outTable->supported[i] = GPU_FEATURE_SUPPORT_NATIVE;
                    else if (strncmp(valPos, "emulated", 8) == 0) outTable->supported[i] = GPU_FEATURE_SUPPORT_EMULATED;
                    else outTable->supported[i] = GPU_FEATURE_SUPPORT_UNSUPPORTED;
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

            const char* reasonPos = strstr(pos, "\"reason\"");
            if (reasonPos) {
                const char* strPos = strstr(reasonPos, ":");
                if (strPos) {
                    while (*strPos && (*strPos == ':' || *strPos == ' ' || *strPos == '"')) strPos++;
                    size_t j = 0;
                    while (*strPos && *strPos != '"' && j < 127) {
                        outTable->reasons[i][j++] = *strPos++;
                    }
                    outTable->reasons[i][j] = '\0';
                }
            }
        }
    }

    return GPU_SUCCESS;
}

GpuResult gpuGetFeatureInfo(GpuDevice device, GpuFeature feature, GpuFeatureInfo* outInfo)
{
    if (!device || !outInfo || (int)feature < 0 || feature >= GPU_FEATURE_COUNT)
        return GPU_ERROR_INVALID_ARGS;

    GpuFeatureTable table;
    GpuResult res = gpuBuildFeatureTable(device, &table);
    if (res != GPU_SUCCESS) return res;

    outInfo->support = table.supported[feature];
    outInfo->limit = table.limits[feature];
    outInfo->reason = table.reasons[feature][0] ? table.reasons[feature] : NULL;

    return GPU_SUCCESS;
}

const char* gpuFeatureSupportToString(GpuFeatureSupport support)
{
    switch (support) {
    case GPU_FEATURE_SUPPORT_UNSUPPORTED: return "unsupported";
    case GPU_FEATURE_SUPPORT_EMULATED:    return "emulated";
    case GPU_FEATURE_SUPPORT_NATIVE:      return "native";
    default:                              return "unknown";
    }
}
