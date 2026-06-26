#include "gpu/bindless/gpu_descriptor_set.h"
#include "gpu/bindless/gpu_bindless_heap.h"
#include "gpu/bindless/gpu_descriptor_alloc.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/pipeline/gpu_cmd_bind.h"
#include "gpu/debug/gpu_validation.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <vector>
#include <map>
#include <mutex>
#include <string>
#include <cstring>

// ============================================================================
// Descriptor Pool Implementation
// ============================================================================

struct GpuDescriptorBindingEntry {
    GpuDescriptorWriteType type = GPU_DESCRIPTOR_WRITE_BUFFER;
    GpuBufferHandle buffer = {0, 0};
    uint64_t bufferOffset = 0;
    uint64_t bufferRange = 0;
    GpuTextureHandle texture = {0, 0};
    GpuSamplerHandle sampler = {0, 0};
    bool valid = false;
    uint32_t binding = 0;
    std::string bindingName;
};

struct GpuDescriptorSet_t {
    GpuDescriptorPool pool = nullptr;
    uint32_t poolSlot = UINT32_MAX;
    uint32_t setIndex = 0;
    GpuPipelineLayout layout = nullptr;
    std::map<uint64_t, GpuDescriptorBindingEntry> bindings;
};

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

static uint64_t descriptorBindingKey(uint32_t binding, uint32_t arrayIndex)
{
    return ((uint64_t)binding << 32) | arrayIndex;
}

static const GpuBindingRange* findLayoutBinding(GpuPipelineLayout layout, uint32_t setIndex, uint32_t binding)
{
    if (!layout) return nullptr;
    GpuPipelineLayoutInfo info = {};
    if (gpuGetPipelineLayoutInfo(layout, &info) != GPU_SUCCESS) return nullptr;
    for (uint32_t i = 0; i < info.bindingRangeCount; i++) {
        const GpuBindingRange* range = &info.bindingRanges[i];
        if (range->set == setIndex && range->binding == binding) return range;
    }
    return nullptr;
}

static bool bindingKindMatchesWrite(GpuBindingKind kind, GpuDescriptorWriteType writeType)
{
    switch (writeType) {
    case GPU_DESCRIPTOR_WRITE_BUFFER:
        return kind == GPU_BINDING_KIND_CONSTANT_BUFFER ||
               kind == GPU_BINDING_KIND_TYPED_BUFFER ||
               kind == GPU_BINDING_KIND_RAW_BUFFER ||
               kind == GPU_BINDING_KIND_PARAMETER_BLOCK;
    case GPU_DESCRIPTOR_WRITE_TEXTURE:
        return kind == GPU_BINDING_KIND_TEXTURE ||
               kind == GPU_BINDING_KIND_COMBINED_TEXTURE_SAMPLER ||
               kind == GPU_BINDING_KIND_INPUT_RENDER_TARGET;
    case GPU_DESCRIPTOR_WRITE_SAMPLER:
        return kind == GPU_BINDING_KIND_SAMPLER;
    default:
        return false;
    }
}

GpuResult gpuAllocateDescriptorSet(
    GpuDescriptorPool pool,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet* outSet)
{
    if (!pool || !layout || !outSet) return GPU_ERROR_INVALID_ARGS;

    GpuPipelineLayoutInfo info = {};
    if (gpuGetPipelineLayoutInfo(layout, &info) != GPU_SUCCESS) return GPU_ERROR_INVALID_ARGS;

    bool setFound = false;
    for (uint32_t i = 0; i < info.descriptorSetCount; i++) {
        if (info.descriptorSets[i].set == setIndex) {
            setFound = true;
            break;
        }
    }
    if (!setFound) return GPU_ERROR_INVALID_ARGS;

    uint32_t slot = pool->setAllocator.allocate();
    if (slot == UINT32_MAX || slot >= pool->maxSets) return GPU_ERROR_OUT_OF_MEMORY;

    auto* set = new GpuDescriptorSet_t();
    set->pool = pool;
    set->poolSlot = slot;
    set->setIndex = setIndex;
    set->layout = layout;
    pool->setInUse[slot] = true;
    pool->allocatedSets++;
    *outSet = set;
    return GPU_SUCCESS;
}

void gpuFreeDescriptorSet(GpuDescriptorSet set)
{
    if (!set) return;
    GpuDescriptorPool pool = set->pool;
    if (pool && set->poolSlot < pool->maxSets) {
        pool->setInUse[set->poolSlot] = false;
        pool->setBindings[set->poolSlot].clear();
        pool->setAllocator.free(set->poolSlot);
        if (pool->allocatedSets > 0) pool->allocatedSets--;
    }
    delete set;
}

GpuResult gpuUpdateDescriptorSet(
    GpuDescriptorSet set,
    uint32_t binding,
    uint32_t arrayIndex,
    const GpuDescriptorWrite* write)
{
    if (!set || !write) return GPU_ERROR_INVALID_ARGS;

    const GpuBindingRange* range = findLayoutBinding(set->layout, set->setIndex, binding);
    if (!range) {
        GPU_VALIDATE(set->pool->device, GPU_VALIDATION_SEVERITY_ERROR, "DESCRIPTOR_BINDING_OUT_OF_RANGE",
                     "Descriptor binding not found in pipeline layout", "DescriptorSet");
        return GPU_ERROR_INVALID_ARGS;
    }
    if (arrayIndex >= range->count && range->count != UINT32_MAX) {
        GPU_VALIDATE(set->pool->device, GPU_VALIDATION_SEVERITY_ERROR, "DESCRIPTOR_ARRAY_OUT_OF_RANGE",
                     "Descriptor array index out of range", "DescriptorSet");
        return GPU_ERROR_INVALID_ARGS;
    }
    if (!bindingKindMatchesWrite(range->kind, write->type)) {
        GPU_VALIDATE(set->pool->device, GPU_VALIDATION_SEVERITY_ERROR, "DESCRIPTOR_TYPE_MISMATCH",
                     "Descriptor write type does not match layout binding kind", "DescriptorSet");
        return GPU_ERROR_INVALID_ARGS;
    }

    GpuDescriptorBindingEntry entry = {};
    entry.type = write->type;
    entry.valid = true;
    entry.binding = binding;
    switch (write->type) {
    case GPU_DESCRIPTOR_WRITE_BUFFER:
        if (!gpuHandleIsValid(write->buffer)) return GPU_ERROR_INVALID_ARGS;
        entry.buffer = write->buffer;
        entry.bufferOffset = write->bufferOffset;
        entry.bufferRange = write->bufferRange;
        break;
    case GPU_DESCRIPTOR_WRITE_TEXTURE:
        if (!gpuHandleIsValid(write->texture)) return GPU_ERROR_INVALID_ARGS;
        entry.texture = write->texture;
        break;
    case GPU_DESCRIPTOR_WRITE_SAMPLER:
        if (!gpuHandleIsValid(write->sampler)) return GPU_ERROR_INVALID_ARGS;
        entry.sampler = write->sampler;
        break;
    default:
        return GPU_ERROR_INVALID_ARGS;
    }

    set->bindings[descriptorBindingKey(binding, arrayIndex)] = entry;
    return GPU_SUCCESS;
}

GpuResult gpuUpdateDescriptorSetByName(
    GpuDescriptorSet set,
    const char* bindingName,
    const GpuDescriptorWrite* write)
{
    if (!set || !bindingName || !write) return GPU_ERROR_INVALID_ARGS;

    const GpuBindingRange* range = nullptr;
    GpuPipelineLayoutInfo info = {};
    if (gpuGetPipelineLayoutInfo(set->layout, &info) != GPU_SUCCESS) return GPU_ERROR_INVALID_ARGS;
    for (uint32_t i = 0; i < info.bindingRangeCount; i++) {
        const GpuBindingRange* candidate = &info.bindingRanges[i];
        if (candidate->set == set->setIndex && candidate->name &&
            strcmp(candidate->name, bindingName) == 0) {
            range = candidate;
            break;
        }
    }
    if (!range) return GPU_ERROR_NOT_FOUND;
    if (!bindingKindMatchesWrite(range->kind, write->type)) {
        GPU_VALIDATE(set->pool->device, GPU_VALIDATION_SEVERITY_ERROR, "DESCRIPTOR_TYPE_MISMATCH",
                     "Descriptor write type does not match layout binding kind", bindingName);
        return GPU_ERROR_INVALID_ARGS;
    }

    GpuResult result = gpuUpdateDescriptorSet(set, range->binding, 0, write);
    if (result != GPU_SUCCESS) return result;

    auto& entry = set->bindings[descriptorBindingKey(range->binding, 0)];
    entry.bindingName = bindingName;
    return GPU_SUCCESS;
}

static void applyDescriptorSetBinding(
    GpuDevice device,
    rhi::IShaderObject* rootObject,
    uint32_t setIndex,
    const GpuDescriptorBindingEntry& entry)
{
    if (!device || !rootObject || !entry.valid) return;
    rhi::ShaderCursor cursor(rootObject);
    rhi::ShaderCursor sub;
    if (!entry.bindingName.empty()) {
        sub = cursor[entry.bindingName.c_str()];
    }
    if (!sub.isValid()) {
        rhi::ShaderCursor field = cursor[setIndex];
        if (!field.isValid()) return;
        sub = field[entry.binding];
        if (!sub.isValid()) return;
    }

    switch (entry.type) {
    case GPU_DESCRIPTOR_WRITE_BUFFER: {
        rhi::IBuffer* buf = device->bufferPool.resolve(entry.buffer.index, entry.buffer.generation);
        if (!buf) return;
        uint64_t size = entry.bufferRange > 0 ? entry.bufferRange : buf->getDesc().size - entry.bufferOffset;
        sub.setBinding(rhi::Binding(buf, rhi::BufferRange{entry.bufferOffset, size}));
        break;
    }
    case GPU_DESCRIPTOR_WRITE_TEXTURE: {
        rhi::ITexture* tex = device->texturePool.resolve(entry.texture.index, entry.texture.generation);
        if (tex) sub.setBinding(rhi::Binding(tex));
        break;
    }
    case GPU_DESCRIPTOR_WRITE_SAMPLER: {
        rhi::ISampler* sampler = device->samplerPool.resolve(entry.sampler.index, entry.sampler.generation);
        if (sampler) sub.setBinding(rhi::Binding(sampler));
        break;
    }
    default:
        break;
    }
}

void gpuCmdBindDescriptorSet(
    GpuCommandBuffer cmd,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet set)
{
    (void)layout;
    if (!cmd || !set || set->setIndex != setIndex) return;
    if (!cmd->rootShaderObject) return;

    for (const auto& kv : set->bindings) {
        applyDescriptorSetBinding(cmd->device, cmd->rootShaderObject, setIndex, kv.second);
    }
}

void gpuCmdBindDescriptorSetPass(
    GpuRenderPassEncoder pass,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet set)
{
    (void)layout;
    if (!pass || !set || set->setIndex != setIndex || !pass->rootShaderObject) return;
    for (const auto& kv : set->bindings) {
        applyDescriptorSetBinding(pass->device, pass->rootShaderObject, setIndex, kv.second);
    }
}

// ============================================================================
// Extended Bindless Heap API
// ============================================================================
