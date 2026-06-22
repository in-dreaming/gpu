#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

typedef enum {
    GPU_DEBUG_LEVEL_NONE    = 0,
    GPU_DEBUG_LEVEL_ERROR   = 1,
    GPU_DEBUG_LEVEL_WARNING = 2,
    GPU_DEBUG_LEVEL_INFO    = 3,
    GPU_DEBUG_LEVEL_VERBOSE = 4,
} GpuDebugLevel;

typedef enum {
    GPU_VALIDATION_SEVERITY_ERROR   = 0,
    GPU_VALIDATION_SEVERITY_WARNING = 1,
    GPU_VALIDATION_SEVERITY_INFO    = 2,
} GpuValidationSeverity;

typedef struct {
    GpuValidationSeverity severity;
    const char* messageId;
    const char* message;
    const char* objectLabel;
    const char* function;
    const char* file;
    int line;
} GpuValidationMessage;

typedef void (*GpuValidationCallback)(const GpuValidationMessage* msg, void* userData);

void gpuSetValidationCallback(GpuDevice device, GpuValidationCallback callback, void* userData);
void gpuSetDebugCallback(GpuDevice device, void (*callback)(GpuDebugLevel, const char*, void*), void* userData);
const char* gpuGetLastError(void);
GpuDebugLevel gpuGetDebugLevel(GpuDevice device);
void gpuSetDebugLevel(GpuDevice device, GpuDebugLevel level);
bool gpuIsValidationEnabled(GpuDevice device);

#ifdef __cplusplus
}
#endif
