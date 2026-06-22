#pragma once

#include "gpu/debug/gpu_debug.h"
#include "gpu/capability/gpu_feature_gating.h"

#ifdef __cplusplus
extern "C" {
#endif

void gpuEmitValidation(GpuDevice device, GpuValidationSeverity severity, const char* messageId,
                        const char* message, const char* objectLabel,
                        const char* function, const char* file, int line);

#ifdef __cplusplus
}
#endif

#define GPU_VALIDATE(device, severity, msgId, msg, label) \
    gpuEmitValidation(device, severity, msgId, msg, label, __FUNCTION__, __FILE__, __LINE__)

#define GPU_VALIDATE_NULL(device, ptr, label, msgId) do { \
    if (!(ptr)) { \
        GPU_VALIDATE(device, GPU_VALIDATION_SEVERITY_ERROR, msgId, \
                     "Null pointer: " #ptr, label); \
        return GPU_ERROR_INVALID_ARGS; \
    } \
} while(0)

#define GPU_FEATURE_GATE(device, feature, label) do { \
    GpuFeatureInfo _fi; \
    if (gpuGetFeatureInfo(device, feature, &_fi) != GPU_SUCCESS || \
        _fi.support == GPU_FEATURE_SUPPORT_UNSUPPORTED) { \
        GPU_VALIDATE(device, GPU_VALIDATION_SEVERITY_ERROR, "FEATURE_NOT_SUPPORTED", \
                     _fi.reason ? _fi.reason : "Feature not supported", label); \
        return GPU_ERROR_NOT_SUPPORTED; \
    } \
} while(0)
