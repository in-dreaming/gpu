#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

typedef struct {
    const char* appName;
    uint32_t adapterIndex;
    bool enableDebugLayer;
} GpuDeviceDesc;

GpuResult gpuCreateDevice(const GpuDeviceDesc* desc, GpuDevice* outDevice);
void gpuDestroyDevice(GpuDevice device);

#ifdef __cplusplus
}
#endif
