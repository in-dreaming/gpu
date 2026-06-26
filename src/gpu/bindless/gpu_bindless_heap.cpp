#include "gpu/bindless/gpu_bindless_heap.h"
#include "gpu/bindless/gpu_descriptor_set.h"
#include "gpu/bindless/gpu_descriptor_alloc.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/debug/gpu_validation.h"
#include <vector>

struct GpuBindlessRecord {
    bool occupied = false;
    bool isTextureView = false;
    GpuHandle resource = {0, 0};
    rhi::DescriptorHandle descriptor = {};
    rhi::ComPtr<rhi::ITextureView> textureView;
};

static rhi::DescriptorHandleAccess bindlessAccessFromUint(uint32_t access)
{
    return access == GPU_DESCRIPTOR_ACCESS_READ_WRITE
               ? rhi::DescriptorHandleAccess::ReadWrite
               : rhi::DescriptorHandleAccess::Read;
}

static bool bindlessAcquireTextureViewDescriptor(
    GpuDevice device,
    GpuHandle viewHandle,
    rhi::DescriptorHandleAccess access,
    GpuBindlessRecord& record)
{
    if (!device || !viewHandle.index) return false;
    auto* view = device->textureViewPool.resolve(viewHandle.index, viewHandle.generation);
    if (!view) return false;
    record.textureView = view;
    record.isTextureView = true;
    record.resource = viewHandle;
    return SLANG_SUCCEEDED(view->getDescriptorHandle(access, &record.descriptor)) &&
           record.descriptor.type != rhi::DescriptorHandleType::Undefined;
}

struct GpuBindlessHeap_t {
    GpuDevice device;
    bool mixedHeap = false;
    DescriptorAllocator allocator;
    uint32_t maxDescriptors;
    GpuDescriptorType descriptorType;
    uint32_t textureCapacity = 0;
    uint32_t bufferCapacity = 0;
    uint32_t samplerCapacity = 0;
    uint32_t allocatedTextures = 0;
    uint32_t allocatedBuffers = 0;
    uint32_t allocatedSamplers = 0;
    std::vector<GpuBindlessRecord> records;
    std::vector<GpuDescriptorType> slotTypes;
};

GpuResult gpuCreateBindlessHeap(GpuDevice device, const GpuBindlessHeapDesc* desc, GpuBindlessHeap* outHeap)
{
    if (!device || !desc || !outHeap) return GPU_ERROR_INVALID_ARGS;

    GPU_FEATURE_GATE(device, GPU_FEATURE_BINDLESS, "BindlessHeap");

    GpuBindlessHeap heap = new GpuBindlessHeap_t();
    heap->device = device;
    heap->mixedHeap = false;
    heap->maxDescriptors = desc->maxDescriptors;
    heap->descriptorType = desc->descriptorType;
    heap->allocator.init(desc->maxDescriptors);
    heap->records.resize(desc->maxDescriptors);
    heap->slotTypes.assign(desc->maxDescriptors, desc->descriptorType);

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

    if (heap->mixedHeap) {
        return UINT32_MAX;
    }

    if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_BUFFER) {
        return gpuBindlessAllocateBuffer(heap, resource, GPU_DESCRIPTOR_ACCESS_READ);
    }

    uint32_t index = heap->allocator.allocate();
    if (index == UINT32_MAX || index >= heap->records.size()) return UINT32_MAX;

    GpuBindlessRecord record = {};
    record.occupied = true;
    record.isTextureView = false;
    record.resource = resource;

    if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_TEXTURE) {
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
            record.descriptor.type == rhi::DescriptorHandleType::Undefined) {
            heap->allocator.free(index);
            return UINT32_MAX;
        }
    } else if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_SAMPLER) {
        rhi::ISampler* sampler = heap->device->samplerPool.resolve(resource.index, resource.generation);
        if (!sampler) {
            heap->allocator.free(index);
            return UINT32_MAX;
        }
        if (SLANG_FAILED(sampler->getDescriptorHandle(&record.descriptor)) ||
            record.descriptor.type == rhi::DescriptorHandleType::Undefined) {
            heap->allocator.free(index);
            return UINT32_MAX;
        }
    } else {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    heap->records[index] = record;
    heap->slotTypes[index] = heap->descriptorType;
    if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_TEXTURE) heap->allocatedTextures++;
    else if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_BUFFER) heap->allocatedBuffers++;
    else if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_SAMPLER) heap->allocatedSamplers++;

    // Register in device's bindless resource map for gpuGetBindlessIndex lookups
    {
        auto key = ((uint64_t)resource.index << 32) | resource.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap[key] = {heap, index};
    }

    return index;
}

uint32_t gpuBindlessAllocateBuffer(GpuBindlessHeap heap, GpuHandle bufferHandle, uint32_t access)
{
    if (!heap || !bufferHandle.index) return UINT32_MAX;
    if (!heap->mixedHeap && heap->descriptorType != GPU_DESCRIPTOR_TYPE_BUFFER) return UINT32_MAX;
    if (heap->mixedHeap && heap->allocatedBuffers >= heap->bufferCapacity) return UINT32_MAX;

    uint32_t index = heap->allocator.allocate();
    if (index == UINT32_MAX || index >= heap->records.size()) return UINT32_MAX;

    rhi::IBuffer* buffer = heap->device->bufferPool.resolve(bufferHandle.index, bufferHandle.generation);
    if (!buffer) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    GpuBindlessRecord record = {};
    record.occupied = true;
    record.isTextureView = false;
    record.resource = bufferHandle;
    if (SLANG_FAILED(buffer->getDescriptorHandle(
            bindlessAccessFromUint(access),
            rhi::Format::Undefined,
            rhi::kEntireBuffer,
            &record.descriptor)) ||
        record.descriptor.type == rhi::DescriptorHandleType::Undefined) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    heap->records[index] = record;
    heap->slotTypes[index] = GPU_DESCRIPTOR_TYPE_BUFFER;
    heap->allocatedBuffers++;

    {
        auto key = ((uint64_t)bufferHandle.index << 32) | bufferHandle.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap[key] = {heap, index};
    }

    return index;
}

uint32_t gpuBindlessAllocateTextureView(GpuBindlessHeap heap, GpuHandle viewHandle, uint32_t access)
{
    if (!heap || !viewHandle.index) return UINT32_MAX;
    if (!heap->mixedHeap && heap->descriptorType != GPU_DESCRIPTOR_TYPE_TEXTURE) return UINT32_MAX;
    if (heap->mixedHeap && heap->allocatedTextures >= heap->textureCapacity) return UINT32_MAX;

    uint32_t index = heap->allocator.allocate();
    if (index == UINT32_MAX || index >= heap->records.size()) return UINT32_MAX;

    GpuBindlessRecord record = {};
    record.occupied = true;
    if (!bindlessAcquireTextureViewDescriptor(heap->device, viewHandle, bindlessAccessFromUint(access), record)) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    heap->records[index] = record;
    heap->slotTypes[index] = GPU_DESCRIPTOR_TYPE_TEXTURE;
    heap->allocatedTextures++;

    {
        auto key = ((uint64_t)viewHandle.index << 32) | viewHandle.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap[key] = {heap, index};
    }

    return index;
}

uint32_t gpuBindlessAllocateCombinedTextureView(GpuBindlessHeap heap, GpuHandle viewHandle)
{
    if (!heap || !viewHandle.index) return UINT32_MAX;
    if (!heap->mixedHeap && heap->descriptorType != GPU_DESCRIPTOR_TYPE_TEXTURE) return UINT32_MAX;
    if (heap->mixedHeap && heap->allocatedTextures >= heap->textureCapacity) return UINT32_MAX;

    uint32_t index = heap->allocator.allocate();
    if (index == UINT32_MAX || index >= heap->records.size()) return UINT32_MAX;

    auto* view = heap->device->textureViewPool.resolve(viewHandle.index, viewHandle.generation);
    if (!view) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    GpuBindlessRecord record = {};
    record.occupied = true;
    record.isTextureView = true;
    record.resource = viewHandle;
    record.textureView = view;
    if (SLANG_FAILED(view->getCombinedTextureSamplerDescriptorHandle(&record.descriptor)) ||
        record.descriptor.type != rhi::DescriptorHandleType::CombinedTextureSampler) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    heap->records[index] = record;
    heap->slotTypes[index] = GPU_DESCRIPTOR_TYPE_TEXTURE;
    heap->allocatedTextures++;

    {
        auto key = ((uint64_t)viewHandle.index << 32) | viewHandle.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap[key] = {heap, index};
    }

    return index;
}

GpuResult gpuBindlessUpdateTextureView(GpuBindlessHeap heap, uint32_t index, GpuHandle viewHandle, uint32_t access)
{
    if (!heap || !viewHandle.index || index >= heap->records.size()) {
        return GPU_ERROR_INVALID_ARGS;
    }
    if (!heap->mixedHeap && heap->descriptorType != GPU_DESCRIPTOR_TYPE_TEXTURE) {
        return GPU_ERROR_INVALID_ARGS;
    }
    if (heap->mixedHeap && heap->slotTypes[index] != GPU_DESCRIPTOR_TYPE_TEXTURE) {
        return GPU_ERROR_INVALID_ARGS;
    }

    GpuBindlessRecord& record = heap->records[index];
    if (!record.occupied) return GPU_ERROR_INVALID_ARGS;

    {
        auto oldKey = ((uint64_t)record.resource.index << 32) | record.resource.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap.erase(oldKey);
    }

    GpuBindlessRecord updated = {};
    updated.occupied = true;
    if (!bindlessAcquireTextureViewDescriptor(heap->device, viewHandle, bindlessAccessFromUint(access), updated)) {
        record = {};
        return GPU_ERROR_INTERNAL;
    }

    record = updated;

    {
        auto key = ((uint64_t)viewHandle.index << 32) | viewHandle.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap[key] = {heap, index};
    }

    return GPU_SUCCESS;
}

// Internal free without mutex (called from gpuBindlessFree and destroy paths)
static void bindlessFreeInternal(GpuBindlessHeap heap, uint32_t index)
{
    if (!heap || index >= heap->records.size()) return;
    if (heap->records[index].occupied) {
        switch (heap->slotTypes[index]) {
        case GPU_DESCRIPTOR_TYPE_TEXTURE:
            if (heap->allocatedTextures > 0) heap->allocatedTextures--;
            break;
        case GPU_DESCRIPTOR_TYPE_BUFFER:
            if (heap->allocatedBuffers > 0) heap->allocatedBuffers--;
            break;
        case GPU_DESCRIPTOR_TYPE_SAMPLER:
            if (heap->allocatedSamplers > 0) heap->allocatedSamplers--;
            break;
        default:
            break;
        }
    }
    heap->records[index] = GpuBindlessRecord{};
    heap->allocator.free(index);
}

void gpuBindlessFree(GpuBindlessHeap heap, uint32_t index)
{
    if (!heap || index >= heap->records.size()) return;
    // Unregister from device's bindless resource map
    {
        auto& rec = heap->records[index];
        if (rec.occupied) {
            auto key = ((uint64_t)rec.resource.index << 32) | rec.resource.generation;
            std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
            heap->device->bindlessResourceMap.erase(key);
        }
    }
    bindlessFreeInternal(heap, index);
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
    if (!record.occupied || record.descriptor.type == rhi::DescriptorHandleType::Undefined) return GPU_ERROR_INVALID_ARGS;
    outInfo->type = static_cast<uint32_t>(record.descriptor.type);
    outInfo->value = record.descriptor.value;
    return GPU_SUCCESS;
}

uint32_t gpuGetBindlessIndex(GpuDevice device, GpuHandle handle)
{
    if (!device || !gpuHandleIsValid(handle)) return UINT32_MAX;
    std::lock_guard<std::mutex> lock(device->bindlessMutex);
    auto key = ((uint64_t)handle.index << 32) | handle.generation;
    auto it = device->bindlessResourceMap.find(key);
    if (it != device->bindlessResourceMap.end()) {
        return it->second.index;
    }
    return UINT32_MAX;
}

// ============================================================================
// Phase D: Extended bindless heap API
// ============================================================================

void gpuGetBindlessHeapStats(GpuBindlessHeap heap, GpuBindlessHeapStats* outStats)
{
    if (!heap || !outStats) return;
    outStats->capacity = heap->maxDescriptors;
    if (heap->mixedHeap) {
        outStats->allocatedTextures = heap->allocatedTextures;
        outStats->allocatedBuffers = heap->allocatedBuffers;
        outStats->allocatedSamplers = heap->allocatedSamplers;
        outStats->totalAllocated = heap->allocatedTextures + heap->allocatedBuffers + heap->allocatedSamplers;
        return;
    }

    outStats->allocatedTextures = 0;
    outStats->allocatedBuffers = 0;
    outStats->allocatedSamplers = 0;
    outStats->totalAllocated = 0;

    for (size_t i = 0; i < heap->records.size(); i++) {
        if (heap->records[i].occupied) {
            outStats->totalAllocated++;
            if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_TEXTURE)
                outStats->allocatedTextures++;
            else if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_BUFFER)
                outStats->allocatedBuffers++;
            else if (heap->descriptorType == GPU_DESCRIPTOR_TYPE_SAMPLER)
                outStats->allocatedSamplers++;
        }
    }
}

static uint32_t bindlessAllocateSamplerSlot(GpuBindlessHeap heap, GpuHandle samplerHandle)
{
    if (!heap || !samplerHandle.index) return UINT32_MAX;
    if (!heap->mixedHeap && heap->descriptorType != GPU_DESCRIPTOR_TYPE_SAMPLER) return UINT32_MAX;
    if (heap->mixedHeap && heap->allocatedSamplers >= heap->samplerCapacity) return UINT32_MAX;

    uint32_t index = heap->allocator.allocate();
    if (index == UINT32_MAX || index >= heap->records.size()) return UINT32_MAX;

    rhi::ISampler* sampler = heap->device->samplerPool.resolve(samplerHandle.index, samplerHandle.generation);
    if (!sampler) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    GpuBindlessRecord record = {};
    record.occupied = true;
    record.resource = samplerHandle;
    if (SLANG_FAILED(sampler->getDescriptorHandle(&record.descriptor)) ||
        record.descriptor.type == rhi::DescriptorHandleType::Undefined) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    heap->records[index] = record;
    heap->slotTypes[index] = GPU_DESCRIPTOR_TYPE_SAMPLER;
    heap->allocatedSamplers++;

    {
        auto key = ((uint64_t)samplerHandle.index << 32) | samplerHandle.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap[key] = {heap, index};
    }

    return index;
}

static uint32_t bindlessAllocateTextureSlot(GpuBindlessHeap heap, GpuHandle resource)
{
    if (!heap || !resource.index) return UINT32_MAX;
    if (!heap->mixedHeap && heap->descriptorType != GPU_DESCRIPTOR_TYPE_TEXTURE) return UINT32_MAX;
    if (heap->mixedHeap && heap->allocatedTextures >= heap->textureCapacity) return UINT32_MAX;

    uint32_t index = heap->allocator.allocate();
    if (index == UINT32_MAX || index >= heap->records.size()) return UINT32_MAX;

    rhi::ITexture* texture = heap->device->texturePool.resolve(resource.index, resource.generation);
    if (!texture) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    GpuBindlessRecord record = {};
    record.occupied = true;
    record.resource = resource;
    record.textureView = texture->getDefaultView();
    if (!record.textureView ||
        SLANG_FAILED(record.textureView->getDescriptorHandle(
            rhi::DescriptorHandleAccess::Read,
            &record.descriptor)) ||
        record.descriptor.type == rhi::DescriptorHandleType::Undefined) {
        heap->allocator.free(index);
        return UINT32_MAX;
    }

    heap->records[index] = record;
    heap->slotTypes[index] = GPU_DESCRIPTOR_TYPE_TEXTURE;
    heap->allocatedTextures++;

    {
        auto key = ((uint64_t)resource.index << 32) | resource.generation;
        std::lock_guard<std::mutex> lock(heap->device->bindlessMutex);
        heap->device->bindlessResourceMap[key] = {heap, index};
    }

    return index;
}

uint32_t gpuBindlessRegister(GpuBindlessHeap heap, GpuHandle resource, GpuDescriptorType type)
{
    if (!heap || !gpuHandleIsValid(resource)) return UINT32_MAX;
    if (heap->mixedHeap) {
        switch (type) {
        case GPU_DESCRIPTOR_TYPE_TEXTURE:
            return bindlessAllocateTextureSlot(heap, resource);
        case GPU_DESCRIPTOR_TYPE_BUFFER:
            return gpuBindlessAllocateBuffer(heap, resource, GPU_DESCRIPTOR_ACCESS_READ);
        case GPU_DESCRIPTOR_TYPE_SAMPLER:
            return bindlessAllocateSamplerSlot(heap, resource);
        default:
            return UINT32_MAX;
        }
    }
    if (type != heap->descriptorType) return UINT32_MAX;
    return gpuBindlessAllocate(heap, resource);
}

GpuResult gpuBindlessGetSlotType(GpuBindlessHeap heap, uint32_t index, GpuDescriptorType* outType)
{
    if (!heap || !outType || index >= heap->records.size()) return GPU_ERROR_INVALID_ARGS;
    if (!heap->records[index].occupied) return GPU_ERROR_INVALID_ARGS;
    *outType = heap->slotTypes[index];
    return GPU_SUCCESS;
}

GpuResult gpuBindlessValidateSlot(GpuBindlessHeap heap, uint32_t index, GpuDescriptorType expectedType)
{
    if (!heap || index >= heap->records.size()) return GPU_ERROR_INVALID_ARGS;
    if (!heap->records[index].occupied) {
        GPU_VALIDATE(heap->device, GPU_VALIDATION_SEVERITY_ERROR, "BINDLESS_SLOT_EMPTY",
                     "Bindless slot is not allocated", "BindlessHeap");
        return GPU_ERROR_INVALID_ARGS;
    }
    if (heap->slotTypes[index] != expectedType) {
        GPU_VALIDATE(heap->device, GPU_VALIDATION_SEVERITY_ERROR, "BINDLESS_SLOT_TYPE_MISMATCH",
                     "Bindless slot type does not match expected type", "BindlessHeap");
        return GPU_ERROR_INVALID_ARGS;
    }
    return GPU_SUCCESS;
}

void gpuBindlessUnregister(GpuBindlessHeap heap, uint32_t index)
{
    gpuBindlessFree(heap, index);
}

GpuResult gpuCreateBindlessHeap2(GpuDevice device, const GpuBindlessHeapDesc2* desc, GpuBindlessHeap* outHeap)
{
    if (!device || !desc || !outHeap) return GPU_ERROR_INVALID_ARGS;

    GPU_FEATURE_GATE(device, GPU_FEATURE_BINDLESS, "BindlessHeap2");

    uint32_t typeCount = 0;
    if (desc->maxTextures > 0) typeCount++;
    if (desc->maxBuffers > 0) typeCount++;
    if (desc->maxSamplers > 0) typeCount++;

    if (typeCount <= 1) {
        GpuBindlessHeapDesc oldDesc = {};
        if (desc->maxTextures > 0) {
            oldDesc.maxDescriptors = desc->maxTextures;
            oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE;
        } else if (desc->maxBuffers > 0) {
            oldDesc.maxDescriptors = desc->maxBuffers;
            oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_BUFFER;
        } else {
            oldDesc.maxDescriptors = desc->maxSamplers > 0 ? desc->maxSamplers : 64;
            oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_SAMPLER;
        }
        return gpuCreateBindlessHeap(device, &oldDesc, outHeap);
    }

    GpuBindlessHeap heap = new GpuBindlessHeap_t();
    heap->device = device;
    heap->mixedHeap = true;
    heap->maxDescriptors = desc->maxTextures + desc->maxBuffers + desc->maxSamplers;
    heap->descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE;
    heap->textureCapacity = desc->maxTextures;
    heap->bufferCapacity = desc->maxBuffers;
    heap->samplerCapacity = desc->maxSamplers;
    heap->allocator.init(heap->maxDescriptors);
    heap->records.resize(heap->maxDescriptors);
    heap->slotTypes.assign(heap->maxDescriptors, GPU_DESCRIPTOR_TYPE_TEXTURE);
    *outHeap = heap;
    return GPU_SUCCESS;
}
