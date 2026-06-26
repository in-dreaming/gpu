#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_sampler.h"
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
// Descriptor Set - traditional set / binding mode
// ============================================================================

typedef struct GpuDescriptorSet_t* GpuDescriptorSet;

typedef enum {
    GPU_DESCRIPTOR_WRITE_BUFFER = 0,
    GPU_DESCRIPTOR_WRITE_TEXTURE = 1,
    GPU_DESCRIPTOR_WRITE_SAMPLER = 2,
} GpuDescriptorWriteType;

typedef struct {
    GpuDescriptorWriteType type;
    GpuBufferHandle buffer;
    uint64_t bufferOffset;
    uint64_t bufferRange;
    GpuTextureHandle texture;
    GpuSamplerHandle sampler;
} GpuDescriptorWrite;

GpuResult gpuAllocateDescriptorSet(
    GpuDescriptorPool pool,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet* outSet);

void gpuFreeDescriptorSet(GpuDescriptorSet set);

GpuResult gpuUpdateDescriptorSet(
    GpuDescriptorSet set,
    uint32_t binding,
    uint32_t arrayIndex,
    const GpuDescriptorWrite* write);

GpuResult gpuUpdateDescriptorSetByName(
    GpuDescriptorSet set,
    const char* bindingName,
    const GpuDescriptorWrite* write);

void gpuCmdBindDescriptorSet(
    GpuCommandBuffer cmd,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet set);

void gpuCmdBindDescriptorSetPass(
    GpuRenderPassEncoder pass,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet set);

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
GpuResult gpuBindlessGetSlotType(GpuBindlessHeap heap, uint32_t index, GpuDescriptorType* outType);
GpuResult gpuBindlessValidateSlot(GpuBindlessHeap heap, uint32_t index, GpuDescriptorType expectedType);

#ifdef __cplusplus
}
#endif
