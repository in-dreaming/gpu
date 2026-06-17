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

GpuResult gpuCreateBindlessHeap(GpuDevice device, const GpuBindlessHeapDesc* desc, GpuBindlessHeap* outHeap);
void gpuDestroyBindlessHeap(GpuBindlessHeap heap);

uint32_t gpuBindlessAllocate(GpuBindlessHeap heap, GpuHandle resource);
void gpuBindlessFree(GpuBindlessHeap heap, uint32_t index);

uint32_t gpuGetBindlessIndex(GpuDevice device, GpuHandle handle);

#ifdef __cplusplus
}
#endif
