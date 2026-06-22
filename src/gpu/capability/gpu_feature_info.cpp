#include "gpu/capability/gpu_feature_info.h"
#include "gpu/core/gpu_internal.h"
#include <string.h>

GpuResult gpuGetFeatureInfo(GpuDevice device, GpuFeature feature, GpuFeatureInfo* outInfo)
{
    if (!device || !outInfo || (int)feature < 0 || feature >= GPU_FEATURE_COUNT)
        return GPU_ERROR_INVALID_ARGS;

    GpuFeatureTable table;
    GpuResult res = gpuBuildFeatureTable(device, &table);
    if (res != GPU_SUCCESS) return res;

    outInfo->support = table.support[feature];
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
