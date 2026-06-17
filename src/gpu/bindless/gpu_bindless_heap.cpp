#include "gpu/bindless/gpu_bindless_heap.h"
#include "gpu/bindless/gpu_descriptor_alloc.h"
#include "gpu/core/gpu_internal.h"

struct GpuBindlessHeap_t {
    GpuDevice device;
    DescriptorAllocator allocator;
    uint32_t maxDescriptors;
    GpuDescriptorType descriptorType;
};

GpuResult gpuCreateBindlessHeap(GpuDevice device, const GpuBindlessHeapDesc* desc, GpuBindlessHeap* outHeap)
{
    if (!device || !desc || !outHeap) return GPU_ERROR_INVALID_ARGS;

    GpuBindlessHeap heap = new GpuBindlessHeap_t();
    heap->device = device;
    heap->maxDescriptors = desc->maxDescriptors;
    heap->descriptorType = desc->descriptorType;
    heap->allocator.init(desc->maxDescriptors);

    *outHeap = heap;
    return GPU_SUCCESS;
}

void gpuDestroyBindlessHeap(GpuBindlessHeap heap)
{
    if (!heap) return;
    delete heap;
}

uint32_t gpuBindlessAllocate(GpuBindlessHeap heap, GpuHandle resource)
{
    if (!heap) return UINT32_MAX;
    (void)resource;
    return heap->allocator.allocate();
}

void gpuBindlessFree(GpuBindlessHeap heap, uint32_t index)
{
    if (!heap) return;
    heap->allocator.free(index);
}

uint32_t gpuGetBindlessIndex(GpuDevice device, GpuHandle handle)
{
    if (!device) return UINT32_MAX;
    (void)device;
    return handle.index;
}
