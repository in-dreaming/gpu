#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuResourceManager_t* GpuResourceManager;

GpuResult gpuResourceManagerCreate(GpuDevice device, GpuResourceManager* outMgr);
GpuResult gpuResourceManagerFlush(GpuResourceManager mgr, uint64_t completedFenceValue);
void gpuResourceManagerDestroy(GpuResourceManager mgr);

#ifdef __cplusplus
}
#endif
