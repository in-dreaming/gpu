#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuBindlessHeap_t* GpuBindlessHeap;

typedef uint32_t GpuDescriptorType;
#define GPU_DESCRIPTOR_TYPE_TEXTURE       0u
#define GPU_DESCRIPTOR_TYPE_BUFFER        1u
#define GPU_DESCRIPTOR_TYPE_SAMPLER       2u

typedef struct {
    uint32_t maxDescriptors;
    GpuDescriptorType descriptorType;
} GpuBindlessHeapDesc;

typedef struct {
    uint32_t type;
    uint64_t value;
} GpuDescriptorHandleInfo;

GpuResult gpuCreateBindlessHeap(GpuDevice device, const GpuBindlessHeapDesc* desc, GpuBindlessHeap* outHeap);
void gpuDestroyBindlessHeap(GpuBindlessHeap heap);

uint32_t gpuBindlessAllocate(GpuBindlessHeap heap, GpuHandle resource);
void gpuBindlessFree(GpuBindlessHeap heap, uint32_t index);
bool gpuBindlessIsAllocated(GpuBindlessHeap heap, uint32_t index);
GpuResult gpuBindlessGetResource(GpuBindlessHeap heap, uint32_t index, GpuHandle* outResource);
GpuResult gpuBindlessGetDescriptorHandle(GpuBindlessHeap heap, uint32_t index, GpuDescriptorHandleInfo* outInfo);

uint32_t gpuGetBindlessIndex(GpuDevice device, GpuHandle handle);

#ifdef __cplusplus
}
#endif
