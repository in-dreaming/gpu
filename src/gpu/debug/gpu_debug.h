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

typedef void (*GpuDebugCallback)(GpuDebugLevel level, const char* message, void* userData);

void gpuSetDebugCallback(GpuDevice device, GpuDebugCallback callback, void* userData);
const char* gpuGetLastError(void);
GpuDebugLevel gpuGetDebugLevel(GpuDevice device);
void gpuSetDebugLevel(GpuDevice device, GpuDebugLevel level);

#ifdef __cplusplus
}
#endif
