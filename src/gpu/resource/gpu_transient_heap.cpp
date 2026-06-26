#include "gpu/resource/gpu_transient_heap.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/core/gpu_backend.h"

bool gpuDeviceSupportsTransientHeap(GpuDevice device)
{
    if (!device) return false;
    if (gpuGetBackendType(device) != GPU_BACKEND_VULKAN) return false;

    rhi::HeapDesc desc = {};
    desc.memoryType = rhi::MemoryType::DeviceLocal;
    desc.label = "gpu_transient_heap_probe";
    rhi::ComPtr<rhi::IHeap> heap;
    return SLANG_SUCCEEDED(device->rhiDevice->createHeap(desc, heap.writeRef()));
}

static rhi::BufferDesc gpuBufferDescToRhi(const GpuBufferDesc& desc)
{
    rhi::BufferDesc rhiDesc = {};
    rhiDesc.size = desc.size;
    rhiDesc.elementSize = desc.elementSize;
    rhiDesc.label = desc.label;
    rhiDesc.memoryType = rhi::MemoryType::DeviceLocal;
    rhiDesc.defaultState = rhi::ResourceState::Undefined;

    if (desc.usage & GPU_BUFFER_USAGE_VERTEX_BUFFER) rhiDesc.usage |= rhi::BufferUsage::VertexBuffer;
    if (desc.usage & GPU_BUFFER_USAGE_INDEX_BUFFER) rhiDesc.usage |= rhi::BufferUsage::IndexBuffer;
    if (desc.usage & GPU_BUFFER_USAGE_CONSTANT_BUFFER) rhiDesc.usage |= rhi::BufferUsage::ConstantBuffer;
    if (desc.usage & GPU_BUFFER_USAGE_SHADER_RESOURCE) rhiDesc.usage |= rhi::BufferUsage::ShaderResource;
    if (desc.usage & GPU_BUFFER_USAGE_UNORDERED_ACCESS) rhiDesc.usage |= rhi::BufferUsage::UnorderedAccess;
    if (desc.usage & GPU_BUFFER_USAGE_INDIRECT_ARGUMENT) rhiDesc.usage |= rhi::BufferUsage::IndirectArgument;
    if (desc.usage & GPU_BUFFER_USAGE_COPY_SOURCE) rhiDesc.usage |= rhi::BufferUsage::CopySource;
    if (desc.usage & GPU_BUFFER_USAGE_COPY_DEST) rhiDesc.usage |= rhi::BufferUsage::CopyDestination;
    if (desc.usage & GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE) rhiDesc.usage |= rhi::BufferUsage::AccelerationStructure;
    return rhiDesc;
}

GpuResult gpuCreateBufferFromHeapAlloc(GpuDevice device, void* heap, void* heapAlloc,
                                       const GpuBufferDesc* desc, GpuBufferHandle* outHandle)
{
    if (!device || !heap || !heapAlloc || !desc || !outHandle) return GPU_ERROR_INVALID_ARGS;

    auto* rhiHeap = static_cast<rhi::IHeap*>(heap);
    auto* alloc = static_cast<rhi::HeapAlloc*>(heapAlloc);
    if (!alloc->isValid()) return GPU_ERROR_INVALID_ARGS;

    rhi::BufferDesc rhiDesc = gpuBufferDescToRhi(*desc);
    rhi::ComPtr<rhi::IBuffer> rhiBuf;
    if (SLANG_FAILED(device->rhiDevice->createBufferFromHeapAllocation(rhiHeap, *alloc, rhiDesc, rhiBuf.writeRef())))
        return GPU_ERROR_NOT_SUPPORTED;

    uint32_t idx = device->bufferPool.allocate(rhiBuf.detach());
    if (idx == 0) return GPU_ERROR_OUT_OF_MEMORY;
    *outHandle = {idx, device->bufferPool.slots[idx].generation};
    return GPU_SUCCESS;
}

void gpuReleaseHeapAlloc(GpuDevice device, void* heap, void* heapAlloc)
{
    if (!device || !heap || !heapAlloc) return;
    auto* rhiHeap = static_cast<rhi::IHeap*>(heap);
    auto* alloc = static_cast<rhi::HeapAlloc*>(heapAlloc);
    if (alloc->isValid())
        rhiHeap->free(*alloc);
    alloc->address = 0;
    alloc->size = 0;
}
