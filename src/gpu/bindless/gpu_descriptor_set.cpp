#include "gpu/bindless/gpu_descriptor_set.h"
#include "gpu/bindless/gpu_bindless_heap.h"
#include "gpu/bindless/gpu_descriptor_alloc.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/debug/gpu_validation.h"
#include <vector>
#include <map>
#include <mutex>
#include <string>

// ============================================================================
// Descriptor Pool Implementation
// ============================================================================

struct GpuDescriptorPool_t {
    GpuDevice device;
    uint32_t maxSets;
    uint32_t maxBindingsPerSet;
    DescriptorAllocator setAllocator;
    std::vector<std::vector<rhi::DescriptorHandle>> setBindings;
    std::vector<bool> setInUse;
    uint32_t allocatedSets;
    uint32_t allocatedBindings;
};

GpuResult gpuCreateDescriptorPool(GpuDevice device, const GpuDescriptorPoolDesc* desc, GpuDescriptorPool* outPool)
{
    if (!device || !desc || !outPool) return GPU_ERROR_INVALID_ARGS;

    GpuDescriptorPool pool = new GpuDescriptorPool_t();
    pool->device = device;
    pool->maxSets = desc->maxSets > 0 ? desc->maxSets : 1024;
    pool->maxBindingsPerSet = desc->maxBindingsPerSet > 0 ? desc->maxBindingsPerSet : 64;
    pool->setAllocator.init(pool->maxSets);
    pool->setBindings.resize(pool->maxSets);
    pool->setInUse.assign(pool->maxSets, false);
    pool->allocatedSets = 0;
    pool->allocatedBindings = 0;

    *outPool = pool;
    return GPU_SUCCESS;
}

void gpuDestroyDescriptorPool(GpuDescriptorPool pool)
{
    if (!pool) return;
    delete pool;
}

void gpuGetDescriptorPoolStats(GpuDescriptorPool pool, GpuDescriptorPoolStats* outStats)
{
    if (!pool || !outStats) return;
    outStats->allocatedSets = pool->allocatedSets;
    outStats->freeSets = pool->maxSets - pool->allocatedSets;
    outStats->totalBindings = pool->maxSets * pool->maxBindingsPerSet;
    outStats->allocatedBindings = pool->allocatedBindings;
}

// ============================================================================
// Extended Bindless Heap API
// ============================================================================

GpuResult gpuCreateBindlessHeap2(GpuDevice device, const GpuBindlessHeapDesc2* desc, GpuBindlessHeap* outHeap)
{
    if (!device || !desc || !outHeap) return GPU_ERROR_INVALID_ARGS;

    GPU_FEATURE_GATE(device, GPU_FEATURE_BINDLESS, "BindlessHeap2");

    GpuBindlessHeapDesc oldDesc = {};
    if (desc->maxTextures > 0 && desc->maxBuffers == 0 && desc->maxSamplers == 0) {
        oldDesc.maxDescriptors = desc->maxTextures;
        oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE;
    } else if (desc->maxBuffers > 0 && desc->maxTextures == 0 && desc->maxSamplers == 0) {
        oldDesc.maxDescriptors = desc->maxBuffers;
        oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_BUFFER;
    } else if (desc->maxSamplers > 0 && desc->maxTextures == 0 && desc->maxBuffers == 0) {
        oldDesc.maxDescriptors = desc->maxSamplers;
        oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_SAMPLER;
    } else {
        uint32_t maxTotal = desc->maxTextures + desc->maxBuffers + desc->maxSamplers;
        oldDesc.maxDescriptors = maxTotal;
        if (desc->maxTextures >= desc->maxBuffers && desc->maxTextures >= desc->maxSamplers) {
            oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE;
        } else if (desc->maxBuffers >= desc->maxSamplers) {
            oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_BUFFER;
        } else {
            oldDesc.descriptorType = GPU_DESCRIPTOR_TYPE_SAMPLER;
        }
    }

    return gpuCreateBindlessHeap(device, &oldDesc, outHeap);
}
