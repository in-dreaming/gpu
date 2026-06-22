#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/layout/gpu_pipeline_layout.h"
#include "gpu/bindless/gpu_bindless_heap.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuDescriptorPool_t* GpuDescriptorPool;

// ============================================================================
// Descriptor Pool - manages descriptor allocation for non-bindless descriptor sets
// ============================================================================

typedef struct {
    uint32_t maxSets;            // maximum number of descriptor sets
    uint32_t maxBindingsPerSet;  // maximum bindings per set
} GpuDescriptorPoolDesc;

GpuResult gpuCreateDescriptorPool(GpuDevice device, const GpuDescriptorPoolDesc* desc, GpuDescriptorPool* outPool);
void gpuDestroyDescriptorPool(GpuDescriptorPool pool);

// Get pool statistics
typedef struct {
    uint32_t allocatedSets;
    uint32_t freeSets;
    uint32_t totalBindings;
    uint32_t allocatedBindings;
} GpuDescriptorPoolStats;

void gpuGetDescriptorPoolStats(GpuDescriptorPool pool, GpuDescriptorPoolStats* outStats);

// ============================================================================
// Bindless Heap - Extended API (Phase D)
// ============================================================================

// Extended bindless heap desc supporting mixed-type heaps
typedef struct {
    uint32_t maxTextures;       // max texture/sampler descriptors
    uint32_t maxBuffers;        // max buffer descriptors
    uint32_t maxSamplers;       // max sampler descriptors
    bool shaderVisible;         // whether heap is shader-visible
} GpuBindlessHeapDesc2;

GpuResult gpuCreateBindlessHeap2(GpuDevice device, const GpuBindlessHeapDesc2* desc, GpuBindlessHeap* outHeap);

// Bindless heap statistics
typedef struct {
    uint32_t allocatedTextures;
    uint32_t allocatedBuffers;
    uint32_t allocatedSamplers;
    uint32_t totalAllocated;
    uint32_t capacity;
} GpuBindlessHeapStats;

void gpuGetBindlessHeapStats(GpuBindlessHeap heap, GpuBindlessHeapStats* outStats);

// Register a resource in the bindless heap and get its index
// This is the real implementation of gpuGetBindlessIndex that actually tracks allocations
uint32_t gpuBindlessRegister(GpuBindlessHeap heap, GpuHandle resource, GpuDescriptorType type);
void gpuBindlessUnregister(GpuBindlessHeap heap, uint32_t index);

#ifdef __cplusplus
}
#endif
