#include "gpu/bindless/gpu_bindless_heap.h"
#include "gpu/bindless/gpu_descriptor_alloc.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/debug/gpu_validation.h"
#include <vector>

struct GpuBindlessRecord {
    bool occupied = false;
    GpuHandle resource = {0, 0};
    rhi::DescriptorHandle descriptor = {};
    rhi::ComPtr<rhi::ITextureView> textureView;
};

struct GpuBindlessHeap_t {
    GpuDevice device;
    DescriptorAllocator allocator;
    uint32_t maxDescriptors;
    GpuDescriptorType descriptorType;
    std::vector<GpuBindlessRecord> records;
};

GpuResult gpuCreateBindlessHeap(GpuDevice device, const GpuBindlessHeapDesc* desc, GpuBindlessHeap* outHeap)
{
    if (!device || !desc || !outHeap) return GPU_ERROR_INVALID_ARGS;

    GPU_FEATURE_GATE(device, GPU_FEATURE_BINDLESS, "BindlessHeap");

    GpuBindlessHeap heap = new GpuBindlessHeap_t();
    heap->device = device;
    heap->maxDescriptors = desc->maxDescriptors;
    heap->descriptorType = desc->descriptorType;
    heap->allocator.init(desc->maxDescriptors);
    heap->records.resize(desc->maxDescriptors);

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
    if (!heap || !gpuHandleIsValid(resource)) return UINT32_MAX;

    uint32_t index = heap->allocator.allocate();
    if (index == UINT32_MAX || index >= heap->records.size()) return UINT32_MAX;

    GpuBindlessRecord record = {};
    record.occupied = true;
    record.resource = resource;

    if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_BUFFER) {
        rhi::IBuffer* buffer = heap->device->bufferPool.resolve(resource.index, resource.generation);
        if (!buffer) {
            heap->allocator.free(index);
            return UINT32_MAX;
        }
        if (SLANG_FAILED(buffer->getDescriptorHandle(
                rhi::DescriptorHandleAccess::Read,
                rhi::Format::Undefined,
                rhi::kEntireBuffer,
                &record.descriptor)) ||
            !record.descriptor) {
            heap->allocator.free(index);
            return UINT32_MAX;
        }
    } else if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_TEXTURE) {
        rhi::ITexture* texture = heap->device->texturePool.resolve(resource.index, resource.generation);
        if (!texture) {
            heap->allocator.free(index);
            return UINT32_MAX;
        }
        record.textureView = texture->getDefaultView();
        if (!record.textureView ||
            SLANG_FAILED(record.textureView->getDescriptorHandle(
                rhi::DescriptorHandleAccess::Read,
                &record.descriptor)) ||
            !record.descriptor) {
            heap->allocator.free(index);
            return UINT32_MAX;
        }
    } else {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    heap->records[index] = record;
    return index;
}

void gpuBindlessFree(GpuBindlessHeap heap, uint32_t index)
{
    if (!heap || index >= heap->records.size()) return;
    heap->records[index] = GpuBindlessRecord{};
    heap->allocator.free(index);
}

bool gpuBindlessIsAllocated(GpuBindlessHeap heap, uint32_t index)
{
    if (!heap || index >= heap->records.size()) return false;
    return heap->records[index].occupied;
}

GpuResult gpuBindlessGetResource(GpuBindlessHeap heap, uint32_t index, GpuHandle* outResource)
{
    if (!heap || !outResource || index >= heap->records.size()) return GPU_ERROR_INVALID_ARGS;
    const GpuBindlessRecord& record = heap->records[index];
    if (!record.occupied) return GPU_ERROR_INVALID_ARGS;
    *outResource = record.resource;
    return GPU_SUCCESS;
}

GpuResult gpuBindlessGetDescriptorHandle(GpuBindlessHeap heap, uint32_t index, GpuDescriptorHandleInfo* outInfo)
{
    if (!heap || !outInfo || index >= heap->records.size()) return GPU_ERROR_INVALID_ARGS;
    const GpuBindlessRecord& record = heap->records[index];
    if (!record.occupied || !record.descriptor) return GPU_ERROR_INVALID_ARGS;
    outInfo->type = static_cast<uint32_t>(record.descriptor.type);
    outInfo->value = record.descriptor.value;
    return GPU_SUCCESS;
}

uint32_t gpuGetBindlessIndex(GpuDevice device, GpuHandle handle)
{
    if (!device) return UINT32_MAX;
    (void)device;
    return handle.index;
}
