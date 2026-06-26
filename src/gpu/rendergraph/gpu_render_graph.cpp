#include "gpu/rendergraph/gpu_render_graph.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/debug/gpu_debug_markers.h"
#include "gpu/queue/gpu_multi_queue.h"
#include "gpu/sync/gpu_fence.h"
#include "gpu/resource/gpu_hazard.h"
#include "gpu/resource/gpu_copy.h"
#include "gpu/resource/gpu_transient_heap.h"
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <memory>
#include <cstddef>
#include <cstdio>
#include <chrono>
#include <thread>

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

struct GpuGraphResourceRecord {
    GpuGraphResourceKind kind;
    std::string name;
    bool imported;
    bool isSurfaceTexture;

    GpuTextureDesc textureDesc;
    GpuBufferDesc bufferDesc;

    GpuTextureHandle importedTexture;
    GpuBufferHandle importedBuffer;
    GpuSurfaceTexture importedSurfaceTexture;
    GpuResourceState initialState;

    GpuTextureHandle realizedTexture;
    GpuBufferHandle realizedBuffer;
    GpuTextureHandle realizedView;    // color render target view
    GpuTextureHandle depthView;       // depth-stencil view

    GpuResourceState currentState;
    uint32_t firstPassIndex;
    uint32_t lastPassIndex;
    uint32_t poolTextureIndex;
    uint32_t poolBufferIndex;
    uint32_t transientAllocationId;
    bool aliased;

    GpuAccessFlags lastAccess;
    uint32_t lastWriterPass;
    bool hadWriter;
    bool heapPlaced;
    rhi::HeapAlloc heapAlloc;
};

struct GpuCompiledCopyOp {
    GpuBufferHandle src;
    GpuBufferHandle dst;
    uint64_t size;
    bool valid;
};

struct GpuGraphPassAccess {
    GpuGraphResource resource;
    GpuGraphAccess access;
    uint32_t mipLevel;
    uint32_t arrayLayer;
};

struct GpuGraphPass_t {
    std::string name;
    GpuGraphPassKind kind;
    std::vector<GpuGraphPassAccess> accesses;

    std::vector<GpuGraphColorAttachment> colorAttachments;
    GpuGraphDepthAttachment depthAttachment;
    bool hasDepth;

    GpuGraphPassCallback callback;
    void* userData;

    bool culled;
    std::string culledReason;
    std::vector<uint32_t> dependencies;
    uint32_t topoOrder;
};

struct GpuCompiledBarrier {
    bool isTexture;
    bool isGlobalBarrier;
    GpuTextureHandle tex;
    GpuBufferHandle buf;
    GpuResourceState before;
    GpuResourceState after;
    GpuAccessFlags access;
    uint32_t resourceIndex;
    uint32_t srcPassIndex;
    uint32_t destPassIndex;
    uint32_t mipLevel;
    uint32_t mipCount;
    uint32_t arrayLayer;
    uint32_t arrayCount;
    GpuHazardKind hazardKind;
    GpuQueueType srcQueue;
    GpuQueueType destQueue;
    bool queueOwnershipTransfer;
};

struct GpuGraph_t {
    GpuDevice device;
    std::vector<GpuGraphResourceRecord> resources;
    std::vector<std::unique_ptr<GpuGraphPass_t>> passes;
    bool compiled;
    std::vector<std::vector<GpuCompiledBarrier>> passBarriers;
    std::vector<uint32_t> executionOrder;
    GpuGraphExecuteMode executeMode;
    GpuGraphExecuteMode effectiveExecuteMode;
    bool enablePassProfiling;
    GpuQueryPool timestampPool;
    uint32_t timestampQueryCount;
    std::vector<uint32_t> passProfileBeginQuery;
    std::vector<uint32_t> passProfileEndQuery;
    std::vector<GpuCompiledBarrier> flatBarriers;
    std::vector<std::string> validationWarnings;
    std::vector<GpuCompiledCopyOp> passCopyOps;
    rhi::ComPtr<rhi::IHeap> transientHeap;
    std::vector<rhi::HeapAlloc> transientHeapAllocs;
    bool useTransientHeap;
};

GpuResult gpuGraphCreate(GpuDevice device, GpuGraph* outGraph)
{
    if (!device || !outGraph) return GPU_ERROR_INVALID_ARGS;
    auto* g = new GpuGraph_t{};
    g->device = device;
    g->compiled = false;
    g->executeMode = GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER;
    g->effectiveExecuteMode = GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER;
    g->enablePassProfiling = false;
    g->timestampPool = nullptr;
    g->timestampQueryCount = 0;
    g->useTransientHeap = false;
    *outGraph = g;
    return GPU_SUCCESS;
}

static bool textureDescEqual(const GpuTextureDesc& a, const GpuTextureDesc& b)
{
    return memcmp(&a, &b, sizeof(GpuTextureDesc)) == 0;
}

static bool bufferDescEqual(const GpuBufferDesc& a, const GpuBufferDesc& b)
{
    return memcmp(&a, &b, sizeof(GpuBufferDesc)) == 0;
}

static GpuResult realizeTransientTexture(GpuDevice device, GpuGraphResourceRecord& res);
static GpuResult realizeTransientBuffer(GpuGraph graph, GpuGraphResourceRecord& res);

static GpuQueueType queueTypeForPassKind(GpuGraphPassKind kind)
{
    switch (kind) {
    case GPU_GRAPH_PASS_COMPUTE: return GPU_QUEUE_TYPE_COMPUTE;
    case GPU_GRAPH_PASS_COPY: return GPU_QUEUE_TYPE_TRANSFER;
    default: return GPU_QUEUE_TYPE_GRAPHICS;
    }
}

static void assignLifetimeAliasing(GpuGraph graph)
{
    auto assignForKind = [&](GpuGraphResourceKind kind, bool isTexture) {
        struct AllocSlot {
            uint32_t id;
            uint32_t lastPass;
            std::vector<uint32_t> resourceIndices;
        };

        std::vector<uint32_t> transientIndices;
        for (size_t i = 0; i < graph->resources.size(); i++) {
            auto& r = graph->resources[i];
            if (r.imported || r.kind != kind) continue;
            if (isTexture && r.isSurfaceTexture) continue;
            if (r.firstPassIndex == UINT32_MAX) continue;
            transientIndices.push_back((uint32_t)i);
        }

        std::sort(transientIndices.begin(), transientIndices.end(),
                  [&](uint32_t a, uint32_t b) {
                      return graph->resources[a].firstPassIndex < graph->resources[b].firstPassIndex;
                  });

        std::vector<AllocSlot> slots;
        for (uint32_t ri : transientIndices) {
            auto& res = graph->resources[ri];
            bool assigned = false;
            for (auto& slot : slots) {
                const auto& ref = graph->resources[slot.resourceIndices[0]];
                bool sameDesc = isTexture
                    ? textureDescEqual(ref.textureDesc, res.textureDesc)
                    : bufferDescEqual(ref.bufferDesc, res.bufferDesc);
                if (!sameDesc) continue;
                if (slot.lastPass < res.firstPassIndex) {
                    res.transientAllocationId = slot.id;
                    res.aliased = !slot.resourceIndices.empty();
                    slot.resourceIndices.push_back(ri);
                    slot.lastPass = std::max(slot.lastPass, res.lastPassIndex);
                    assigned = true;
                    break;
                }
            }
            if (!assigned) {
                AllocSlot slot = {};
                slot.id = (uint32_t)slots.size();
                slot.lastPass = res.lastPassIndex;
                slot.resourceIndices.push_back(ri);
                res.transientAllocationId = slot.id;
                res.aliased = false;
                slots.push_back(slot);
            }
        }
    };

    assignForKind(GPU_GRAPH_RESOURCE_TEXTURE, true);
    assignForKind(GPU_GRAPH_RESOURCE_BUFFER, false);
}

static void realizeAliasedTransientTextures(GpuGraph graph)
{
    std::unordered_map<uint32_t, uint32_t> primaryByAlloc;
    for (size_t i = 0; i < graph->resources.size(); i++) {
        auto& res = graph->resources[i];
        if (res.imported || res.kind != GPU_GRAPH_RESOURCE_TEXTURE || res.isSurfaceTexture) continue;
        if (res.transientAllocationId == UINT32_MAX) continue;

        auto it = primaryByAlloc.find(res.transientAllocationId);
        if (it == primaryByAlloc.end()) {
            GpuResult r = realizeTransientTexture(graph->device, res);
            if (r != GPU_SUCCESS) continue;
            primaryByAlloc[res.transientAllocationId] = (uint32_t)i;
        } else {
            auto& primary = graph->resources[it->second];
            res.realizedTexture = primary.realizedTexture;
            res.realizedView = primary.realizedView;
            res.poolTextureIndex = primary.poolTextureIndex;
            res.aliased = true;
        }
    }
}

static void realizeAliasedTransientBuffers(GpuGraph graph)
{
    std::unordered_map<uint32_t, uint32_t> primaryByAlloc;
    for (size_t i = 0; i < graph->resources.size(); i++) {
        auto& res = graph->resources[i];
        if (res.imported || res.kind != GPU_GRAPH_RESOURCE_BUFFER) continue;
        if (res.transientAllocationId == UINT32_MAX) continue;

        auto it = primaryByAlloc.find(res.transientAllocationId);
        if (it == primaryByAlloc.end()) {
            GpuResult r = realizeTransientBuffer(graph, res);
            if (r != GPU_SUCCESS) continue;
            primaryByAlloc[res.transientAllocationId] = (uint32_t)i;
        } else {
            auto& primary = graph->resources[it->second];
            res.realizedBuffer = primary.realizedBuffer;
            res.poolBufferIndex = primary.poolBufferIndex;
            res.aliased = true;
        }
    }
}

static void releaseTransientTexture(GpuDevice device, GpuGraphResourceRecord& res)
{
    if (!device || res.imported) return;
    if (res.poolTextureIndex < device->pooledTransientTextures.size()) {
        device->pooledTransientTextures[res.poolTextureIndex].inUse = false;
    } else {
        if (res.realizedView.index != 0) gpuDestroyTextureView(device, res.realizedView);
        if (res.depthView.index != 0) gpuDestroyTextureView(device, res.depthView);
        if (res.realizedTexture.index != 0) gpuDestroyTexture(device, res.realizedTexture);
    }
    res.realizedTexture = GPU_NULL_HANDLE;
    res.realizedBuffer = GPU_NULL_HANDLE;
    res.realizedView = GPU_NULL_HANDLE;
    res.depthView = GPU_NULL_HANDLE;
    res.poolTextureIndex = UINT32_MAX;
}

static void releaseTransientBuffer(GpuGraph graph, GpuGraphResourceRecord& res)
{
    if (!graph || res.imported) return;
    GpuDevice device = graph->device;
    if (res.heapPlaced && graph->transientHeap && res.heapAlloc.isValid()) {
        if (res.realizedBuffer.index != 0)
            gpuDestroyBuffer(device, res.realizedBuffer);
        graph->transientHeap->free(res.heapAlloc);
        res.heapPlaced = false;
        res.heapAlloc = {};
        res.realizedBuffer = GPU_NULL_HANDLE;
        res.poolBufferIndex = UINT32_MAX;
        return;
    }
    if (res.poolBufferIndex < device->pooledTransientBuffers.size()) {
        device->pooledTransientBuffers[res.poolBufferIndex].inUse = false;
    } else if (res.realizedBuffer.index != 0) {
        gpuDestroyBuffer(device, res.realizedBuffer);
    }
    res.realizedBuffer = GPU_NULL_HANDLE;
    res.poolBufferIndex = UINT32_MAX;
}

static GpuResult realizeTransientTexture(GpuDevice device, GpuGraphResourceRecord& res)
{
    for (size_t i = 0; i < device->pooledTransientTextures.size(); i++) {
        auto& entry = device->pooledTransientTextures[i];
        if (!entry.inUse && textureDescEqual(entry.desc, res.textureDesc)) {
            entry.inUse = true;
            res.realizedTexture = entry.texture;
            res.realizedView = entry.rtView;
            res.poolTextureIndex = (uint32_t)i;
            return GPU_SUCCESS;
        }
    }

    GpuPooledTransientTexture entry = {};
    entry.desc = res.textureDesc;
    entry.inUse = true;
    GpuResult r = gpuCreateTexture(device, &res.textureDesc, &entry.texture);
    if (r != GPU_SUCCESS) return r;
    if (res.textureDesc.usage & GPU_TEXTURE_USAGE_RENDER_TARGET) {
        r = gpuCreateTextureView(device, entry.texture, GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &entry.rtView);
        if (r != GPU_SUCCESS) {
            gpuDestroyTexture(device, entry.texture);
            return r;
        }
    }
    res.realizedTexture = entry.texture;
    res.realizedView = entry.rtView;
    res.poolTextureIndex = (uint32_t)device->pooledTransientTextures.size();
    device->pooledTransientTextures.push_back(entry);
    return GPU_SUCCESS;
}

static GpuResult realizeTransientBuffer(GpuGraph graph, GpuGraphResourceRecord& res)
{
    GpuDevice device = graph->device;

    if (graph->useTransientHeap && graph->transientHeap) {
        rhi::HeapAllocDesc allocDesc = {};
        allocDesc.size = res.bufferDesc.size;
        allocDesc.alignment = 256;
        rhi::HeapAlloc alloc = {};
        if (SLANG_SUCCEEDED(graph->transientHeap->allocate(allocDesc, &alloc))) {
            GpuBufferHandle handle = {};
            if (gpuCreateBufferFromHeapAlloc(device, graph->transientHeap.get(), &alloc,
                                             &res.bufferDesc, &handle) == GPU_SUCCESS) {
                res.realizedBuffer = handle;
                res.heapPlaced = true;
                res.heapAlloc = alloc;
                res.poolBufferIndex = UINT32_MAX;
                graph->transientHeapAllocs.push_back(alloc);
                return GPU_SUCCESS;
            }
            graph->transientHeap->free(alloc);
        }
    }

    for (size_t i = 0; i < device->pooledTransientBuffers.size(); i++) {
        auto& entry = device->pooledTransientBuffers[i];
        if (!entry.inUse && bufferDescEqual(entry.desc, res.bufferDesc)) {
            entry.inUse = true;
            res.realizedBuffer = entry.buffer;
            res.poolBufferIndex = (uint32_t)i;
            return GPU_SUCCESS;
        }
    }

    GpuPooledTransientBuffer entry = {};
    entry.desc = res.bufferDesc;
    entry.inUse = true;
    GpuResult r = gpuCreateBuffer(device, &res.bufferDesc, &entry.buffer);
    if (r != GPU_SUCCESS) return r;
    res.realizedBuffer = entry.buffer;
    res.poolBufferIndex = (uint32_t)device->pooledTransientBuffers.size();
    device->pooledTransientBuffers.push_back(entry);
    return GPU_SUCCESS;
}

static void releaseGraphResource(GpuGraph graph, GpuGraphResourceRecord& res)
{
    if (res.kind == GPU_GRAPH_RESOURCE_TEXTURE) {
        if (!res.imported) {
            releaseTransientTexture(graph->device, res);
        } else {
            if (res.depthView.index != 0) gpuDestroyTextureView(graph->device, res.depthView);
            if (!res.isSurfaceTexture && res.realizedView.index != 0)
                gpuDestroyTextureView(graph->device, res.realizedView);
        }
    } else if (!res.imported) {
        releaseTransientBuffer(graph, res);
    }
}

void gpuGraphDestroy(GpuGraph graph)
{
    if (!graph) return;
    if (graph->timestampPool) {
        gpuDestroyQueryPool(graph->device, graph->timestampPool);
        graph->timestampPool = nullptr;
    }
    if (graph->compiled) {
        for (auto& res : graph->resources) {
            releaseGraphResource(graph, res);
        }
    }
    graph->transientHeap = nullptr;
    graph->transientHeapAllocs.clear();
    graph->useTransientHeap = false;
    delete graph;
}

void gpuGraphReset(GpuGraph graph)
{
    if (!graph) return;
    if (graph->compiled) {
        for (auto& res : graph->resources) {
            releaseGraphResource(graph, res);
        }
    }
    graph->resources.clear();
    graph->passes.clear();
    graph->compiled = false;
    graph->passBarriers.clear();
    graph->passCopyOps.clear();
    graph->executionOrder.clear();
    graph->effectiveExecuteMode = graph->executeMode;
    graph->passProfileBeginQuery.clear();
    graph->passProfileEndQuery.clear();
    graph->flatBarriers.clear();
    graph->validationWarnings.clear();
    graph->transientHeap = nullptr;
    graph->transientHeapAllocs.clear();
    graph->useTransientHeap = false;
    if (graph->timestampPool) {
        gpuDestroyQueryPool(graph->device, graph->timestampPool);
        graph->timestampPool = nullptr;
        graph->timestampQueryCount = 0;
    }
}

static GpuGraphResource allocResource(GpuGraph graph, GpuGraphResourceKind kind, const char* name)
{
    uint32_t idx = (uint32_t)graph->resources.size();
    graph->resources.push_back({});
    auto& r = graph->resources.back();
    r.kind = kind;
    r.name = name ? name : "";
    r.imported = false;
    r.isSurfaceTexture = false;
    r.initialState = GPU_RESOURCE_STATE_UNDEFINED;
    r.realizedTexture = GPU_NULL_HANDLE;
    r.realizedBuffer = GPU_NULL_HANDLE;
    r.realizedView = GPU_NULL_HANDLE;
    r.currentState = GPU_RESOURCE_STATE_UNDEFINED;
    r.firstPassIndex = UINT32_MAX;
    r.lastPassIndex = 0;
    r.poolTextureIndex = UINT32_MAX;
    r.poolBufferIndex = UINT32_MAX;
    r.transientAllocationId = UINT32_MAX;
    r.aliased = false;
    r.lastAccess = GPU_ACCESS_NONE;
    r.lastWriterPass = UINT32_MAX;
    r.hadWriter = false;
    r.heapPlaced = false;
    r.heapAlloc = {};
    return idx + 1;
}

GpuGraphResource gpuGraphCreateTexture(GpuGraph graph, const GpuTextureDesc* desc, const char* name)
{
    if (!graph || !desc) return GPU_GRAPH_NULL_RESOURCE;
    GpuGraphResource r = allocResource(graph, GPU_GRAPH_RESOURCE_TEXTURE, name);
    graph->resources[r - 1].textureDesc = *desc;
    return r;
}

GpuGraphResource gpuGraphCreateBuffer(GpuGraph graph, const GpuBufferDesc* desc, const char* name)
{
    if (!graph || !desc) return GPU_GRAPH_NULL_RESOURCE;
    GpuGraphResource r = allocResource(graph, GPU_GRAPH_RESOURCE_BUFFER, name);
    graph->resources[r - 1].bufferDesc = *desc;
    return r;
}

GpuGraphResource gpuGraphImportTexture(GpuGraph graph, GpuTextureHandle handle, GpuResourceState initialState, const char* name)
{
    if (!graph || handle.index == 0) return GPU_GRAPH_NULL_RESOURCE;
    GpuGraphResource r = allocResource(graph, GPU_GRAPH_RESOURCE_TEXTURE, name);
    auto& rec = graph->resources[r - 1];
    rec.imported = true;
    rec.importedTexture = handle;
    rec.initialState = initialState;
    rec.currentState = initialState;
    rec.realizedTexture = handle;
    return r;
}

GpuGraphResource gpuGraphImportBuffer(GpuGraph graph, GpuBufferHandle handle, GpuResourceState initialState, const char* name)
{
    if (!graph || handle.index == 0) return GPU_GRAPH_NULL_RESOURCE;
    GpuGraphResource r = allocResource(graph, GPU_GRAPH_RESOURCE_BUFFER, name);
    auto& rec = graph->resources[r - 1];
    rec.imported = true;
    rec.importedBuffer = handle;
    rec.initialState = initialState;
    rec.currentState = initialState;
    rec.realizedBuffer = handle;
    if (rhi::IBuffer* buf = graph->device->bufferPool.resolve(handle.index, handle.generation)) {
        const auto& d = buf->getDesc();
        rec.bufferDesc.size = d.size;
        rec.bufferDesc.elementSize = d.elementSize ? d.elementSize : 4;
    }
    return r;
}

GpuGraphResource gpuGraphImportSurfaceTexture(GpuGraph graph, GpuSurfaceTexture surfaceTex, const char* name)
{
    if (!graph || !surfaceTex) return GPU_GRAPH_NULL_RESOURCE;
    GpuGraphResource r = allocResource(graph, GPU_GRAPH_RESOURCE_TEXTURE, name);
    auto& rec = graph->resources[r - 1];
    rec.imported = true;
    rec.isSurfaceTexture = true;
    rec.importedSurfaceTexture = surfaceTex;
    rec.initialState = GPU_RESOURCE_STATE_PRESENT;
    rec.currentState = GPU_RESOURCE_STATE_PRESENT;
    return r;
}

GpuGraphPass gpuGraphAddRenderPass(GpuGraph graph, const char* name)
{
    if (!graph) return nullptr;
    auto* p = new GpuGraphPass_t{};
    p->name = name ? name : "";
    p->kind = GPU_GRAPH_PASS_RENDER;
    p->hasDepth = false;
    p->callback = nullptr;
    p->userData = nullptr;
    p->culled = false;
    p->culledReason.clear();
    graph->passes.emplace_back(p);
    return p;
}

GpuGraphPass gpuGraphAddComputePass(GpuGraph graph, const char* name)
{
    if (!graph) return nullptr;
    auto* p = new GpuGraphPass_t{};
    p->name = name ? name : "";
    p->kind = GPU_GRAPH_PASS_COMPUTE;
    p->hasDepth = false;
    p->callback = nullptr;
    p->userData = nullptr;
    p->culled = false;
    p->culledReason.clear();
    graph->passes.emplace_back(p);
    return p;
}

GpuGraphPass gpuGraphAddCopyPass(GpuGraph graph, const char* name)
{
    if (!graph) return nullptr;
    auto* p = new GpuGraphPass_t{};
    p->name = name ? name : "";
    p->kind = GPU_GRAPH_PASS_COPY;
    p->hasDepth = false;
    p->callback = nullptr;
    p->userData = nullptr;
    p->culled = false;
    p->culledReason.clear();
    graph->passes.emplace_back(p);
    return p;
}

static void addAccess(GpuGraphPass pass, GpuGraphResource resource, GpuGraphAccess access,
                      uint32_t mipLevel, uint32_t arrayLayer)
{
    if (!pass || resource == GPU_GRAPH_NULL_RESOURCE) return;
    for (auto& a : pass->accesses) {
        if (a.resource == resource && a.mipLevel == mipLevel && a.arrayLayer == arrayLayer) {
            if (access == GPU_GRAPH_ACCESS_WRITE || access == GPU_GRAPH_ACCESS_READ_WRITE)
                a.access = access;
            return;
        }
    }
    pass->accesses.push_back({resource, access, mipLevel, arrayLayer});
}

void gpuGraphPassRead(GpuGraphPass pass, GpuGraphResource resource)
{
    addAccess(pass, resource, GPU_GRAPH_ACCESS_READ, 0, 0);
}
void gpuGraphPassWrite(GpuGraphPass pass, GpuGraphResource resource)
{
    addAccess(pass, resource, GPU_GRAPH_ACCESS_WRITE, 0, 0);
}
void gpuGraphPassReadWrite(GpuGraphPass pass, GpuGraphResource resource)
{
    addAccess(pass, resource, GPU_GRAPH_ACCESS_READ_WRITE, 0, 0);
}

void gpuGraphPassReadSubresource(GpuGraphPass pass, GpuGraphResource resource,
                                 uint32_t mipLevel, uint32_t arrayLayer)
{
    addAccess(pass, resource, GPU_GRAPH_ACCESS_READ, mipLevel, arrayLayer);
}

void gpuGraphPassWriteSubresource(GpuGraphPass pass, GpuGraphResource resource,
                                  uint32_t mipLevel, uint32_t arrayLayer)
{
    addAccess(pass, resource, GPU_GRAPH_ACCESS_WRITE, mipLevel, arrayLayer);
}

void gpuGraphPassPresent(GpuGraphPass pass, GpuGraphResource resource)
{
    addAccess(pass, resource, GPU_GRAPH_ACCESS_PRESENT, 0, 0);
}

void gpuGraphPassSetColorAttachments(GpuGraphPass pass, uint32_t count, const GpuGraphColorAttachment* attachments)
{
    if (!pass || !attachments) return;
    pass->colorAttachments.assign(attachments, attachments + count);
    for (uint32_t i = 0; i < count; i++)
        addAccess(pass, attachments[i].resource, GPU_GRAPH_ACCESS_WRITE, 0, 0);
}

void gpuGraphPassSetDepthAttachment(GpuGraphPass pass, const GpuGraphDepthAttachment* attachment)
{
    if (!pass || !attachment) return;
    pass->depthAttachment = *attachment;
    pass->hasDepth = true;
    addAccess(pass, attachment->resource, GPU_GRAPH_ACCESS_WRITE, 0, 0);
}

void gpuGraphPassSetCallback(GpuGraphPass pass, GpuGraphPassCallback callback, void* userData)
{
    if (!pass) return;
    pass->callback = callback;
    pass->userData = userData;
}

void gpuGraphSetExecuteMode(GpuGraph graph, GpuGraphExecuteMode mode)
{
    if (!graph) return;
    graph->executeMode = mode;
}

GpuGraphExecuteMode gpuGraphGetExecuteMode(GpuGraph graph)
{
    return graph ? graph->executeMode : GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER;
}

void gpuGraphSetPassProfiling(GpuGraph graph, bool enabled)
{
    if (!graph) return;
    graph->enablePassProfiling = enabled;
}

bool gpuGraphGetPassProfiling(GpuGraph graph)
{
    return graph ? graph->enablePassProfiling : false;
}

static GpuResourceState accessToState(GpuGraphAccess access, GpuGraphResourceKind /*kind*/,
                                       GpuGraphPassKind passKind, bool isColorAttachment, bool isDepthAttachment)
{
    if (access == GPU_GRAPH_ACCESS_PRESENT) return GPU_RESOURCE_STATE_PRESENT;
    if (isColorAttachment) return GPU_RESOURCE_STATE_RENDER_TARGET;
    if (isDepthAttachment) return GPU_RESOURCE_STATE_DEPTH_WRITE;
    if (passKind == GPU_GRAPH_PASS_COPY) {
        if (access == GPU_GRAPH_ACCESS_READ) return GPU_RESOURCE_STATE_COPY_SOURCE;
        return GPU_RESOURCE_STATE_COPY_DEST;
    }
    if (access == GPU_GRAPH_ACCESS_WRITE || access == GPU_GRAPH_ACCESS_READ_WRITE)
        return GPU_RESOURCE_STATE_UNORDERED_ACCESS;
    return GPU_RESOURCE_STATE_SHADER_RESOURCE;
}

static bool graphResourceIsDepthTexture(GpuDevice device, const GpuGraphResourceRecord& res)
{
    if (!device || res.kind != GPU_GRAPH_RESOURCE_TEXTURE) return false;
    GpuTextureHandle tex = res.imported ? res.importedTexture : res.realizedTexture;
    if (!gpuHandleIsValid(tex)) return false;
    rhi::ITexture* rhiTex = device->texturePool.resolve(tex.index, tex.generation);
    if (!rhiTex) return false;
    switch (rhiTex->getDesc().format) {
    case rhi::Format::D32Float:
    case rhi::Format::D16Unorm:
    case rhi::Format::D32FloatS8Uint:
        return true;
    default:
        return false;
    }
}

static GpuResourceState graphReadStateForResource(GpuDevice device, const GpuGraphResourceRecord& res)
{
    // D3D12 depth textures must transition to DEPTH_READ before SRV sampling.
    if (graphResourceIsDepthTexture(device, res)) return GPU_RESOURCE_STATE_DEPTH_READ;
    return GPU_RESOURCE_STATE_SHADER_RESOURCE;
}

GpuResult gpuGraphCompile(GpuGraph graph)
{
    if (!graph) return GPU_ERROR_INVALID_ARGS;
    if (graph->compiled) return GPU_ERROR_INVALID_ARGS;

    size_t passCount = graph->passes.size();
    size_t resCount = graph->resources.size();

    std::vector<std::unordered_set<uint32_t>> passWrites(resCount);
    std::vector<std::unordered_set<uint32_t>> passReads(resCount);

    for (size_t pi = 0; pi < passCount; pi++) {
        auto& pass = *graph->passes[pi];
        uint32_t passIdx = (uint32_t)pi;
        for (auto& acc : pass.accesses) {
            uint32_t ri = acc.resource - 1;
            if (ri >= resCount) continue;
            auto& res = graph->resources[ri];
            if (res.firstPassIndex > passIdx) res.firstPassIndex = passIdx;
            if (res.lastPassIndex < passIdx) res.lastPassIndex = passIdx;
            if (acc.access == GPU_GRAPH_ACCESS_WRITE || acc.access == GPU_GRAPH_ACCESS_READ_WRITE ||
                acc.access == GPU_GRAPH_ACCESS_PRESENT)
                passWrites[ri].insert(passIdx);
            if (acc.access == GPU_GRAPH_ACCESS_READ || acc.access == GPU_GRAPH_ACCESS_READ_WRITE)
                passReads[ri].insert(passIdx);
        }
    }

    for (size_t pi = 0; pi < passCount; pi++) {
        auto& pass = *graph->passes[pi];
        pass.dependencies.clear();
        for (auto& acc : pass.accesses) {
            uint32_t ri = acc.resource - 1;
            if (ri >= resCount) continue;
            if (acc.access == GPU_GRAPH_ACCESS_READ || acc.access == GPU_GRAPH_ACCESS_READ_WRITE) {
                for (uint32_t w : passWrites[ri]) {
                    if (w != (uint32_t)pi) pass.dependencies.push_back(w);
                }
            }
            if (acc.access == GPU_GRAPH_ACCESS_WRITE || acc.access == GPU_GRAPH_ACCESS_READ_WRITE) {
                for (uint32_t w : passWrites[ri]) {
                    if (w != (uint32_t)pi) pass.dependencies.push_back(w);
                }
                for (uint32_t r : passReads[ri]) {
                    if (r != (uint32_t)pi) pass.dependencies.push_back(r);
                }
            }
            if (acc.access == GPU_GRAPH_ACCESS_PRESENT) {
                for (uint32_t w : passWrites[ri]) {
                    if (w != (uint32_t)pi) pass.dependencies.push_back(w);
                }
            }
        }
        std::sort(pass.dependencies.begin(), pass.dependencies.end());
        pass.dependencies.erase(std::unique(pass.dependencies.begin(), pass.dependencies.end()), pass.dependencies.end());
    }

    std::vector<bool> written(passCount, false);
    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t pi = 0; pi < passCount; pi++) {
            if (written[pi]) continue;
            bool allDepsWritten = true;
            for (uint32_t d : graph->passes[pi]->dependencies) {
                if (!written[d]) { allDepsWritten = false; break; }
            }
            if (allDepsWritten) {
                written[pi] = true;
                graph->executionOrder.push_back((uint32_t)pi);
                changed = true;
            }
        }
    }
    for (size_t pi = 0; pi < passCount; pi++) {
        if (!written[pi]) graph->executionOrder.push_back((uint32_t)pi);
    }

    std::vector<bool> resourceNeeded(resCount, false);
    for (int iter = 0; iter < 2; iter++) {
        for (int i = (int)graph->executionOrder.size() - 1; i >= 0; i--) {
            uint32_t pi = graph->executionOrder[i];
            auto& pass = *graph->passes[pi];
            if (pass.culled) continue;
            bool needed = false;
            for (auto& acc : pass.accesses) {
                uint32_t ri = acc.resource - 1;
                if (ri >= resCount) continue;
                if (resourceNeeded[ri]) needed = true;
                if (acc.access == GPU_GRAPH_ACCESS_WRITE || acc.access == GPU_GRAPH_ACCESS_READ_WRITE ||
                    acc.access == GPU_GRAPH_ACCESS_PRESENT) {
                    if (graph->resources[ri].imported) needed = true;
                    resourceNeeded[ri] = true;
                    needed = true;
                }
            }
            if (pass.callback && iter == 0) needed = true;
            if (!needed && !pass.dependencies.empty()) needed = true;
            if (!needed) {
                pass.culled = true;
                pass.culledReason = "no consumers and no side effects";
            }
        }
    }
    for (auto& p : graph->passes) {
        if (p->callback) {
            p->culled = false;
            p->culledReason.clear();
        }
    }

    assignLifetimeAliasing(graph);

    graph->useTransientHeap = gpuDeviceSupportsTransientHeap(graph->device);
    if (graph->useTransientHeap) {
        rhi::HeapDesc heapDesc = {};
        heapDesc.memoryType = rhi::MemoryType::DeviceLocal;
        heapDesc.label = "gpu_graph_transient_heap";
        if (SLANG_FAILED(graph->device->rhiDevice->createHeap(heapDesc, graph->transientHeap.writeRef())))
            graph->useTransientHeap = false;
    }

    // Mark which resources are used as depth attachments
    bool* isDepthResource = (bool*)alloca(resCount * sizeof(bool));
    memset(isDepthResource, 0, resCount * sizeof(bool));
    for (auto& p : graph->passes) {
        if (p->hasDepth && p->depthAttachment.resource > 0 && p->depthAttachment.resource - 1 < resCount) {
            isDepthResource[p->depthAttachment.resource - 1] = true;
        }
    }

    for (auto& res : graph->resources) {
        if (!res.imported && res.kind == GPU_GRAPH_RESOURCE_TEXTURE) {
            // realized below via lifetime aliasing groups
        } else if (res.imported && res.kind == GPU_GRAPH_RESOURCE_TEXTURE && !res.isSurfaceTexture) {
            res.realizedTexture = res.importedTexture;
            if (res.importedTexture.index != 0) {
                rhi::ITexture* tex = graph->device->texturePool.resolve(res.importedTexture.index, res.importedTexture.generation);
                if (tex) {
                    const auto& desc = tex->getDesc();
                    if ((desc.usage & rhi::TextureUsage::RenderTarget) != rhi::TextureUsage::None) {
                        GpuResult vr = gpuCreateTextureView(graph->device, res.importedTexture,
                            GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &res.realizedView);
                        (void)vr;
                    }
                }
            }
        } else if (!res.imported && res.kind == GPU_GRAPH_RESOURCE_BUFFER) {
            // realized below via lifetime aliasing groups
        }
    }

    for (auto& res : graph->resources) {
        if (!res.imported && res.kind == GPU_GRAPH_RESOURCE_TEXTURE && !res.isSurfaceTexture) {
            if (res.transientAllocationId == UINT32_MAX) {
                GpuResult r = realizeTransientTexture(graph->device, res);
                if (r != GPU_SUCCESS) return r;
            }
        } else if (!res.imported && res.kind == GPU_GRAPH_RESOURCE_BUFFER) {
            if (res.transientAllocationId == UINT32_MAX) {
                GpuResult r = realizeTransientBuffer(graph, res);
                if (r != GPU_SUCCESS) return r;
            }
        }
    }
    realizeAliasedTransientTextures(graph);
    realizeAliasedTransientBuffers(graph);

    graph->passBarriers.resize(passCount);
    for (auto& res : graph->resources) {
        res.currentState = res.initialState;
        res.lastAccess = gpuAccessFlagsForResourceState(res.initialState);
        res.lastWriterPass = UINT32_MAX;
        res.hadWriter = false;
    }

    auto graphAccessToFlags = [](GpuGraphAccess access, GpuGraphPassKind passKind) -> GpuAccessFlags {
        if (access == GPU_GRAPH_ACCESS_PRESENT) return GPU_ACCESS_PRESENT;
        if (passKind == GPU_GRAPH_PASS_COPY) {
            if (access == GPU_GRAPH_ACCESS_READ) return GPU_ACCESS_COPY_READ;
            return GPU_ACCESS_COPY_WRITE;
        }
        if (access == GPU_GRAPH_ACCESS_READ) return GPU_ACCESS_SHADER_READ;
        if (access == GPU_GRAPH_ACCESS_WRITE) return GPU_ACCESS_SHADER_WRITE;
        return GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE;
    };

    auto pushBarrier = [&](std::vector<GpuCompiledBarrier>& barriers, GpuGraphResourceRecord& res,
                           uint32_t ri, uint32_t pi, GpuResourceState targetState, GpuAccessFlags access,
                           uint32_t mipLevel, uint32_t arrayLayer) {
        if (gpuHazardNeedsGlobalBarrier(res.currentState, access)) {
            GpuCompiledBarrier global = {};
            global.isGlobalBarrier = true;
            global.destPassIndex = pi;
            global.srcPassIndex = res.lastWriterPass;
            global.hazardKind = GPU_HAZARD_WRITE_AFTER_WRITE;
            barriers.push_back(global);
        }

        GpuHazardKind hazard = gpuHazardClassify(res.currentState, access, res.hadWriter);
        if (hazard != GPU_HAZARD_NONE && hazard != GPU_HAZARD_READ_AFTER_WRITE) {
            char msg[256];
            snprintf(msg, sizeof(msg), "pass %u resource '%s': %s",
                     pi, res.name.c_str(), gpuHazardKindName(hazard));
            graph->validationWarnings.push_back(msg);
        }

        if (res.currentState == targetState &&
            mipLevel == 0 && arrayLayer == 0) {
            res.lastAccess = access;
            if (gpuAccessFlagsIsWrite(access)) {
                res.hadWriter = true;
                res.lastWriterPass = pi;
            }
            return;
        }

        GpuCompiledBarrier b = {};
        b.isTexture = (res.kind == GPU_GRAPH_RESOURCE_TEXTURE);
        if (b.isTexture) b.tex = res.imported ? res.importedTexture : res.realizedTexture;
        else b.buf = res.imported ? res.importedBuffer : res.realizedBuffer;
        b.before = res.currentState;
        b.after = targetState;
        b.access = access;
        b.resourceIndex = ri;
        b.srcPassIndex = res.lastWriterPass;
        b.destPassIndex = pi;
        b.mipLevel = mipLevel;
        b.mipCount = (mipLevel == 0 && arrayLayer == 0) ? 0 : 1;
        b.arrayLayer = arrayLayer;
        b.arrayCount = (mipLevel == 0 && arrayLayer == 0) ? 0 : 1;
        b.hazardKind = hazard;
        b.destQueue = queueTypeForPassKind(graph->passes[pi]->kind);
        b.srcQueue = GPU_QUEUE_TYPE_GRAPHICS;
        if (res.lastWriterPass != UINT32_MAX && res.lastWriterPass < graph->passes.size())
            b.srcQueue = queueTypeForPassKind(graph->passes[res.lastWriterPass]->kind);
        b.queueOwnershipTransfer = res.hadWriter && res.lastWriterPass != UINT32_MAX && b.srcQueue != b.destQueue;
        barriers.push_back(b);
        res.currentState = targetState;
        res.lastAccess = access;
        if (gpuAccessFlagsIsWrite(access)) {
            res.hadWriter = true;
            res.lastWriterPass = pi;
        }
    };

    for (uint32_t si = 0; si < (uint32_t)graph->executionOrder.size(); si++) {
        uint32_t pi = graph->executionOrder[si];
        auto& pass = *graph->passes[pi];
        if (pass.culled) continue;

        auto& barriers = graph->passBarriers[pi];
        barriers.clear();

        for (uint32_t ai = 0; ai < (uint32_t)pass.colorAttachments.size(); ai++) {
            uint32_t ri = pass.colorAttachments[ai].resource - 1;
            if (ri >= resCount) continue;
            auto& res = graph->resources[ri];
            pushBarrier(barriers, res, ri, pi, GPU_RESOURCE_STATE_RENDER_TARGET,
                        GPU_ACCESS_COLOR_ATTACHMENT | GPU_ACCESS_SHADER_WRITE, 0, 0);
        }

        if (pass.hasDepth) {
            uint32_t ri = pass.depthAttachment.resource - 1;
            if (ri < resCount) {
                auto& res = graph->resources[ri];
                pushBarrier(barriers, res, ri, pi, GPU_RESOURCE_STATE_DEPTH_WRITE,
                            GPU_ACCESS_DEPTH_WRITE, 0, 0);
            }
        }

        for (auto& acc : pass.accesses) {
            uint32_t ri = acc.resource - 1;
            if (ri >= resCount) continue;
            auto& res = graph->resources[ri];
            bool isColorAttach = false;
            bool isDepthAttach = false;
            for (auto& ca : pass.colorAttachments) {
                if (ca.resource - 1 == ri) { isColorAttach = true; break; }
            }
            if (pass.hasDepth && pass.depthAttachment.resource - 1 == ri) isDepthAttach = true;
            if (isColorAttach || isDepthAttach) continue;

            GpuAccessFlags access = graphAccessToFlags(acc.access, pass.kind);
            GpuResourceState targetState = accessToState(acc.access, res.kind, pass.kind, false, false);
            if (acc.access == GPU_GRAPH_ACCESS_READ)
                targetState = graphReadStateForResource(graph->device, res);
            if (acc.access == GPU_GRAPH_ACCESS_PRESENT)
                targetState = GPU_RESOURCE_STATE_PRESENT;
            pushBarrier(barriers, res, ri, pi, targetState, access, acc.mipLevel, acc.arrayLayer);
        }
    }

    for (auto& res : graph->resources) {
        if (res.imported && res.initialState != res.currentState) {
            if (!graph->executionOrder.empty()) {
                uint32_t lastPi = graph->executionOrder.back();
                uint32_t ri = (uint32_t)(&res - &graph->resources[0]);
                pushBarrier(graph->passBarriers[lastPi], res, ri, lastPi, res.initialState,
                            gpuAccessFlagsForResourceState(res.initialState), 0, 0);
            }
        }
    }

    if (graph->enablePassProfiling) {
        uint32_t activePasses = 0;
        for (auto& p : graph->passes) if (!p->culled) activePasses++;
        graph->timestampQueryCount = activePasses * 2;
        graph->passProfileBeginQuery.assign(passCount, UINT32_MAX);
        graph->passProfileEndQuery.assign(passCount, UINT32_MAX);
        uint32_t qi = 0;
        for (uint32_t si = 0; si < (uint32_t)graph->executionOrder.size(); si++) {
            uint32_t pi = graph->executionOrder[si];
            if (graph->passes[pi]->culled) continue;
            graph->passProfileBeginQuery[pi] = qi++;
            graph->passProfileEndQuery[pi] = qi++;
        }
        if (graph->timestampPool) {
            gpuDestroyQueryPool(graph->device, graph->timestampPool);
            graph->timestampPool = nullptr;
        }
        if (graph->timestampQueryCount > 0) {
            if (gpuCreateQueryPool(graph->device, graph->timestampQueryCount, &graph->timestampPool) != GPU_SUCCESS) {
                graph->validationWarnings.push_back("failed to create timestamp query pool for pass profiling");
                graph->enablePassProfiling = false;
                graph->timestampQueryCount = 0;
            }
        } else {
            graph->enablePassProfiling = false;
        }
    }

    graph->flatBarriers.clear();
    for (uint32_t si = 0; si < (uint32_t)graph->executionOrder.size(); si++) {
        uint32_t pi = graph->executionOrder[si];
        if (graph->passes[pi]->culled) continue;
        if (pi < graph->passBarriers.size()) {
            auto& barriers = graph->passBarriers[pi];
            graph->flatBarriers.insert(graph->flatBarriers.end(), barriers.begin(), barriers.end());
        }
    }

    graph->effectiveExecuteMode = graph->executeMode;
    if (graph->executeMode == GPU_GRAPH_EXECUTE_MULTI_QUEUE) {
        if (!gpuDeviceSupportsIndependentQueues(graph->device)) {
            graph->validationWarnings.push_back(
                "multi-queue execute mode downgraded: compute/transfer queues alias graphics queue");
            graph->effectiveExecuteMode = GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER;
        }
    }

    graph->passCopyOps.assign(passCount, {});
    for (size_t pi = 0; pi < passCount; pi++) {
        auto& pass = *graph->passes[pi];
        if (pass.culled || pass.kind != GPU_GRAPH_PASS_COPY) continue;

        GpuBufferHandle src = {}, dst = {};
        uint64_t srcSize = 0, dstSize = 0;
        for (auto& acc : pass.accesses) {
            uint32_t ri = acc.resource - 1;
            if (ri >= resCount) continue;
            auto& res = graph->resources[ri];
            if (res.kind != GPU_GRAPH_RESOURCE_BUFFER) continue;
            GpuBufferHandle handle = res.imported ? res.importedBuffer : res.realizedBuffer;
            if (acc.access == GPU_GRAPH_ACCESS_READ) {
                src = handle;
                srcSize = res.bufferDesc.size;
            } else if (acc.access == GPU_GRAPH_ACCESS_WRITE) {
                dst = handle;
                dstSize = res.bufferDesc.size;
            }
        }
        if (src.index != 0 && dst.index != 0) {
            auto& op = graph->passCopyOps[pi];
            op.src = src;
            op.dst = dst;
            op.size = srcSize < dstSize ? srcSize : dstSize;
            op.valid = op.size > 0;
        }
    }

    graph->compiled = true;
    return GPU_SUCCESS;
}

static void emitBarriers(GpuGraph graph, GpuCommandEncoder encoder, GpuQueueType queueType,
                         const std::vector<GpuCompiledBarrier>& barriers)
{
    const bool onTransferQueue = queueType == GPU_QUEUE_TYPE_TRANSFER;
    const bool onComputeQueue = queueType == GPU_QUEUE_TYPE_COMPUTE;
    for (auto& b : barriers) {
        auto updateTrackedState = [&]() {
            if (b.isTexture && b.tex.index != 0) {
                graph->device->textureStates[b.tex.index] = b.after;
            } else if (!b.isTexture && b.buf.index != 0) {
                graph->device->bufferStates[b.buf.index] = b.after;
            }
        };

        if (b.isGlobalBarrier) {
            if (!onTransferQueue) gpuCmdGlobalBarrier(encoder);
            continue;
        }

        if (onTransferQueue || onComputeQueue) {
            if (!onTransferQueue) gpuCmdGlobalBarrier(encoder);
            updateTrackedState();
            continue;
        }

        if (b.isTexture && b.tex.index != 0) {
            if (b.mipCount > 0) {
                gpuCmdSetTextureSubresourceState(graph->device, encoder, b.tex,
                    b.mipLevel, b.arrayLayer, b.after);
            } else {
                gpuCmdSetTextureState(graph->device, encoder, b.tex, b.after);
            }
        } else if (!b.isTexture && b.buf.index != 0) {
            gpuCmdSetBufferState(graph->device, encoder, b.buf, b.after);
        }
    }
}

static void executeGraphPass(GpuGraph graph, GpuCommandEncoder encoder, GpuQueueType queueType,
                             uint32_t pi, uint32_t* profileQueryIndex)
{
    auto& pass = *graph->passes[pi];
    const bool useDebugMarkers = queueType == GPU_QUEUE_TYPE_GRAPHICS;
    GpuMarkerColor markerColor = { 0.2f, 0.6f, 1.0f };
    if (pass.kind == GPU_GRAPH_PASS_COMPUTE) markerColor = { 0.9f, 0.5f, 0.1f };
    else if (pass.kind == GPU_GRAPH_PASS_COPY) markerColor = { 0.4f, 0.9f, 0.4f };

    if (useDebugMarkers) gpuCmdPushDebugGroup(encoder, pass.name.c_str(), markerColor);
    if (graph->enablePassProfiling && graph->timestampPool && profileQueryIndex) {
        gpuCmdWriteTimestampEncoder(encoder, graph->timestampPool, *profileQueryIndex);
        (*profileQueryIndex)++;
    }

    GpuGraphPassContext ctx = {};
    ctx.encoder = encoder;

    if (pass.kind == GPU_GRAPH_PASS_RENDER) {
        GpuRenderPassColorAttachment colorAtts[8];
        for (uint32_t ci = 0; ci < (uint32_t)pass.colorAttachments.size() && ci < 8; ci++) {
            auto& src = pass.colorAttachments[ci];
            auto& dst = colorAtts[ci];
            uint32_t ri = src.resource - 1;
            auto& res = graph->resources[ri];

            memset(&dst, 0, sizeof(dst));
            if (res.isSurfaceTexture && res.importedSurfaceTexture) {
                dst.attachment = res.importedSurfaceTexture;
            } else if (res.realizedView.index != 0) {
                dst.viewHandle = res.realizedView;
            } else {
                dst.textureHandle = res.imported ? res.importedTexture : res.realizedTexture;
            }
            dst.loadOp = src.loadOp;
            dst.storeOp = src.storeOp;
            memcpy(dst.clearValue, src.clearColor, sizeof(float) * 4);
        }

        GpuRenderPassDepthAttachment depthAtt;
        if (pass.hasDepth) {
            uint32_t ri = pass.depthAttachment.resource - 1;
            if (ri < graph->resources.size()) {
                auto& res = graph->resources[ri];
                memset(&depthAtt, 0, sizeof(depthAtt));
                if (gpuHandleIsValid(pass.depthAttachment.depthViewOverride)) {
                    depthAtt.viewHandle = pass.depthAttachment.depthViewOverride;
                } else {
                    if (res.depthView.index == 0) {
                        GpuTextureHandle texH = res.imported ? res.importedTexture : res.realizedTexture;
                        gpuCreateTextureView(graph->device, texH, GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL, &res.depthView);
                    }
                    depthAtt.viewHandle = res.depthView;
                }
                depthAtt.depthLoadOp = pass.depthAttachment.loadOp;
                depthAtt.depthStoreOp = pass.depthAttachment.storeOp;
                depthAtt.clearDepth = pass.depthAttachment.clearDepth;
                depthAtt.stencilLoadOp = GPU_LOAD_OP_DONT_CARE;
                depthAtt.stencilStoreOp = GPU_STORE_OP_DONT_CARE;
                depthAtt.clearStencil = pass.depthAttachment.clearStencil;
            }
        }

        GpuRenderPassDesc passDesc = {};
        passDesc.colorAttachmentCount = (uint32_t)pass.colorAttachments.size();
        passDesc.colorAttachments = colorAtts;
        passDesc.depthAttachment = pass.hasDepth ? &depthAtt : nullptr;

        GpuRenderPassEncoder rpEnc = gpuCmdBeginRenderPass(encoder, &passDesc);
        if (rpEnc) {
            ctx.renderPass = rpEnc;
            if (pass.callback) pass.callback(&ctx, pass.userData);
            gpuCmdEndRenderPass(rpEnc);
        }
    } else if (pass.kind == GPU_GRAPH_PASS_COMPUTE) {
        GpuComputePassEncoder cpEnc = gpuCmdBeginComputePass(encoder);
        if (cpEnc) {
            ctx.computePass = cpEnc;
            if (pass.callback) pass.callback(&ctx, pass.userData);
            gpuCmdEndComputePass(cpEnc);
        }
    } else {
        if (pass.kind == GPU_GRAPH_PASS_COPY && pi < graph->passCopyOps.size()) {
            auto& copyOp = graph->passCopyOps[pi];
            if (copyOp.valid) {
                gpuCmdCopyBuffer(encoder, copyOp.dst, 0, copyOp.src, 0, copyOp.size);
            }
        }
        if (pass.callback) pass.callback(&ctx, pass.userData);
    }

    if (graph->enablePassProfiling && graph->timestampPool && profileQueryIndex) {
        gpuCmdWriteTimestampEncoder(encoder, graph->timestampPool, *profileQueryIndex);
        (*profileQueryIndex)++;
    }
    if (useDebugMarkers) gpuCmdPopDebugGroup(encoder);
}

static GpuResult executeGraphPasses(GpuGraph graph, GpuCommandEncoder encoder, uint32_t* profileQueryIndex)
{
    for (uint32_t si = 0; si < (uint32_t)graph->executionOrder.size(); si++) {
        uint32_t pi = graph->executionOrder[si];
        auto& pass = *graph->passes[pi];
        if (pass.culled) continue;

        emitBarriers(graph, encoder, GPU_QUEUE_TYPE_GRAPHICS, graph->passBarriers[pi]);
        executeGraphPass(graph, encoder, GPU_QUEUE_TYPE_GRAPHICS, pi, profileQueryIndex);
    }
    return GPU_SUCCESS;
}

static GpuResult executeSingleCommandBuffer(GpuGraph graph, GpuCommandQueue queue)
{
    GpuCommandEncoder encoder = gpuBeginCommandEncoder(graph->device, queue);
    if (!encoder) return GPU_ERROR_INTERNAL;

    uint32_t profileQueryIndex = 0;
    GpuResult result = executeGraphPasses(graph, encoder, graph->enablePassProfiling ? &profileQueryIndex : nullptr);
    if (result != GPU_SUCCESS) return result;

    GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
    if (!cmd) return GPU_ERROR_INTERNAL;
    if (gpuQueueSubmit(queue, 1, &cmd) != GPU_SUCCESS) return GPU_ERROR_INTERNAL;
    return GPU_SUCCESS;
}

static GpuResult executePassSubmit(GpuGraph graph, GpuCommandQueue queue)
{
    uint32_t profileQueryIndex = 0;
    for (uint32_t si = 0; si < (uint32_t)graph->executionOrder.size(); si++) {
        uint32_t pi = graph->executionOrder[si];
        auto& pass = *graph->passes[pi];
        if (pass.culled) continue;

        GpuCommandEncoder encoder = gpuBeginCommandEncoder(graph->device, queue);
        if (!encoder) return GPU_ERROR_INTERNAL;

        emitBarriers(graph, encoder, GPU_QUEUE_TYPE_GRAPHICS, graph->passBarriers[pi]);
        executeGraphPass(graph, encoder, GPU_QUEUE_TYPE_GRAPHICS, pi, graph->enablePassProfiling ? &profileQueryIndex : nullptr);

        GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
        if (!cmd) return GPU_ERROR_INTERNAL;
        if (gpuQueueSubmit(queue, 1, &cmd) != GPU_SUCCESS) return GPU_ERROR_INTERNAL;
    }
    return GPU_SUCCESS;
}

static GpuQueueType executionQueueForPassKind(GpuGraphPassKind kind)
{
    return queueTypeForPassKind(kind);
}

static GpuResult executeMultiQueue(GpuGraph graph)
{
    struct Segment {
        GpuQueueType type;
        std::vector<uint32_t> passIndices;
    };

    std::vector<Segment> segments;
    for (uint32_t si = 0; si < (uint32_t)graph->executionOrder.size(); si++) {
        uint32_t pi = graph->executionOrder[si];
        auto& pass = *graph->passes[pi];
        if (pass.culled) continue;
        GpuQueueType qt = executionQueueForPassKind(pass.kind);
        if (!segments.empty() && segments.back().type == qt) {
            segments.back().passIndices.push_back(pi);
        } else {
            segments.push_back({qt, {pi}});
        }
    }

    if (segments.empty()) return GPU_SUCCESS;

    GpuFence fence = nullptr;
    const bool needFence = segments.size() > 1;
    if (needFence) {
        if (gpuCreateFence(graph->device, 0, &fence) != GPU_SUCCESS) return GPU_ERROR_INTERNAL;
    }

    uint32_t profileQueryIndex = 0;
    for (size_t si = 0; si < segments.size(); si++) {
        auto& seg = segments[si];
        GpuCommandQueue queue = nullptr;
        if (gpuGetQueue(graph->device, seg.type, &queue) != GPU_SUCCESS) return GPU_ERROR_INTERNAL;

        GpuCommandEncoder encoder = gpuBeginCommandEncoder(graph->device, queue);
        if (!encoder) return GPU_ERROR_INTERNAL;

        for (uint32_t pi : seg.passIndices) {
            emitBarriers(graph, encoder, seg.type, graph->passBarriers[pi]);
            executeGraphPass(graph, encoder, seg.type, pi, graph->enablePassProfiling ? &profileQueryIndex : nullptr);
        }

        if (si + 1 < segments.size() && segments[si + 1].type != seg.type) {
            gpuCmdGlobalBarrier(encoder);
        }

        GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
        if (!cmd) return GPU_ERROR_INTERNAL;

        GpuSemaphore signalSem = {};
        const GpuSemaphore* signalPtr = nullptr;
        if (needFence && si + 1 < segments.size()) {
            signalSem.fence = fence;
            signalSem.value = (uint64_t)(si + 1);
            signalPtr = &signalSem;
        }

        GpuResult submitRes = gpuQueueSubmitWithSync(queue, 0, nullptr, 1, &cmd, signalPtr);
        if (submitRes != GPU_SUCCESS) {
            if (fence) gpuDestroyFence(graph->device, fence);
            return GPU_ERROR_INTERNAL;
        }

        if (needFence && si + 1 < segments.size()) {
            if (gpuFenceWait(graph->device, fence, (uint64_t)(si + 1), UINT32_MAX) != GPU_SUCCESS) {
                if (fence) gpuDestroyFence(graph->device, fence);
                return GPU_ERROR_INTERNAL;
            }
        }
    }

    if (fence) gpuDestroyFence(graph->device, fence);
    return GPU_SUCCESS;
}

GpuResult gpuGraphExecute(GpuGraph graph, GpuCommandQueue queue)
{
    if (!graph || !queue) return GPU_ERROR_INVALID_ARGS;
    if (!graph->compiled) return GPU_ERROR_INVALID_ARGS;

    switch (graph->effectiveExecuteMode) {
    case GPU_GRAPH_EXECUTE_PASS_SUBMIT:
        return executePassSubmit(graph, queue);
    case GPU_GRAPH_EXECUTE_MULTI_QUEUE:
        return executeMultiQueue(graph);
    case GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER:
    default:
        return executeSingleCommandBuffer(graph, queue);
    }
}

GpuTextureHandle gpuGraphGetTexture(GpuGraph graph, GpuGraphResource resource)
{
    if (!graph || resource == GPU_GRAPH_NULL_RESOURCE) return GPU_NULL_HANDLE;
    uint32_t ri = resource - 1;
    if (ri >= graph->resources.size()) return GPU_NULL_HANDLE;
    auto& res = graph->resources[ri];
    if (res.kind != GPU_GRAPH_RESOURCE_TEXTURE) return GPU_NULL_HANDLE;
    return res.imported ? res.importedTexture : res.realizedTexture;
}

GpuBufferHandle gpuGraphGetBuffer(GpuGraph graph, GpuGraphResource resource)
{
    if (!graph || resource == GPU_GRAPH_NULL_RESOURCE) return GPU_NULL_HANDLE;
    uint32_t ri = resource - 1;
    if (ri >= graph->resources.size()) return GPU_NULL_HANDLE;
    auto& res = graph->resources[ri];
    if (res.kind != GPU_GRAPH_RESOURCE_BUFFER) return GPU_NULL_HANDLE;
    return res.imported ? res.importedBuffer : res.realizedBuffer;
}

GpuGraphPassKind gpuGraphGetPassKind(GpuGraph graph, uint32_t passIndex)
{
    if (!graph || passIndex >= graph->passes.size()) return GPU_GRAPH_PASS_RENDER;
    return graph->passes[passIndex]->kind;
}

const char* gpuGraphGetPassName(GpuGraph graph, uint32_t passIndex)
{
    if (!graph || passIndex >= graph->passes.size()) return "";
    return graph->passes[passIndex]->name.c_str();
}

uint32_t gpuGraphGetPassCount(GpuGraph graph)
{
    return graph ? (uint32_t)graph->passes.size() : 0;
}

uint32_t gpuGraphGetExecutionOrderCount(GpuGraph graph)
{
    return graph ? (uint32_t)graph->executionOrder.size() : 0;
}

uint32_t gpuGraphGetExecutionOrderPassIndex(GpuGraph graph, uint32_t sortedIndex)
{
    if (!graph || sortedIndex >= graph->executionOrder.size()) return UINT32_MAX;
    return graph->executionOrder[sortedIndex];
}

bool gpuGraphIsPassCulled(GpuGraph graph, uint32_t passIndex)
{
    if (!graph || passIndex >= graph->passes.size()) return true;
    return graph->passes[passIndex]->culled;
}

GpuGraphExecuteMode gpuGraphGetEffectiveExecuteMode(GpuGraph graph)
{
    if (!graph) return GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER;
    return graph->compiled ? graph->effectiveExecuteMode : graph->executeMode;
}

GpuResult gpuGraphGetResourceLifetime(GpuGraph graph, GpuGraphResource resource,
                                       GpuGraphResourceLifetimeInfo* outInfo)
{
    if (!graph || !outInfo || resource == GPU_GRAPH_NULL_RESOURCE) return GPU_ERROR_INVALID_ARGS;
    uint32_t ri = resource - 1;
    if (ri >= graph->resources.size()) return GPU_ERROR_INVALID_ARGS;
    auto& res = graph->resources[ri];
    outInfo->firstUsePass = res.firstPassIndex;
    outInfo->lastUsePass = res.lastPassIndex;
    outInfo->allocationId = res.transientAllocationId;
    outInfo->aliased = res.aliased;
    outInfo->imported = res.imported;
    return GPU_SUCCESS;
}

uint32_t gpuGraphGetPassBarrierCount(GpuGraph graph, uint32_t passIndex)
{
    if (!graph || !graph->compiled || passIndex >= graph->passBarriers.size()) return 0;
    return (uint32_t)graph->passBarriers[passIndex].size();
}

GpuResult gpuGraphGetPassBarrier(GpuGraph graph, uint32_t passIndex, uint32_t barrierIndex,
                                 GpuGraphBarrierInfo* outInfo)
{
    if (!graph || !outInfo || !graph->compiled) return GPU_ERROR_INVALID_ARGS;
    if (passIndex >= graph->passBarriers.size()) return GPU_ERROR_INVALID_ARGS;
    auto& barriers = graph->passBarriers[passIndex];
    if (barrierIndex >= barriers.size()) return GPU_ERROR_INVALID_ARGS;

    const auto& b = barriers[barrierIndex];
    outInfo->resourceName = "";
    if (!b.isGlobalBarrier && b.resourceIndex < graph->resources.size())
        outInfo->resourceName = graph->resources[b.resourceIndex].name.c_str();
    outInfo->before = b.before;
    outInfo->after = b.after;
    outInfo->access = b.access;
    outInfo->srcPassIndex = b.srcPassIndex;
    outInfo->destPassIndex = b.destPassIndex;
    outInfo->mipLevel = b.mipLevel;
    outInfo->mipCount = b.mipCount;
    outInfo->arrayLayer = b.arrayLayer;
    outInfo->arrayCount = b.arrayCount;
    outInfo->isGlobalBarrier = b.isGlobalBarrier;
    outInfo->hazardKind = b.hazardKind;
    outInfo->srcQueue = b.srcQueue;
    outInfo->destQueue = b.destQueue;
    outInfo->queueOwnershipTransfer = b.queueOwnershipTransfer;
    return GPU_SUCCESS;
}

static void fillBarrierInfo(GpuGraph graph, const GpuCompiledBarrier& b, GpuGraphBarrierInfo* outInfo)
{
    outInfo->resourceName = "";
    if (!b.isGlobalBarrier && b.resourceIndex < graph->resources.size())
        outInfo->resourceName = graph->resources[b.resourceIndex].name.c_str();
    outInfo->before = b.before;
    outInfo->after = b.after;
    outInfo->access = b.access;
    outInfo->srcPassIndex = b.srcPassIndex;
    outInfo->destPassIndex = b.destPassIndex;
    outInfo->mipLevel = b.mipLevel;
    outInfo->mipCount = b.mipCount;
    outInfo->arrayLayer = b.arrayLayer;
    outInfo->arrayCount = b.arrayCount;
    outInfo->isGlobalBarrier = b.isGlobalBarrier;
    outInfo->hazardKind = b.hazardKind;
    outInfo->srcQueue = b.srcQueue;
    outInfo->destQueue = b.destQueue;
    outInfo->queueOwnershipTransfer = b.queueOwnershipTransfer;
}

uint32_t gpuGraphGetBarrierCount(GpuGraph graph)
{
    if (!graph || !graph->compiled) return 0;
    return (uint32_t)graph->flatBarriers.size();
}

GpuResult gpuGraphGetBarrier(GpuGraph graph, uint32_t barrierIndex, GpuGraphBarrierInfo* outInfo)
{
    if (!graph || !outInfo || !graph->compiled) return GPU_ERROR_INVALID_ARGS;
    if (barrierIndex >= graph->flatBarriers.size()) return GPU_ERROR_INVALID_ARGS;
    fillBarrierInfo(graph, graph->flatBarriers[barrierIndex], outInfo);
    return GPU_SUCCESS;
}

uint32_t gpuGraphGetTransientAllocationCount(GpuGraph graph)
{
    if (!graph || !graph->compiled) return 0;
    uint32_t count = 0;
    for (auto& res : graph->resources) {
        if (!res.imported && res.transientAllocationId != UINT32_MAX)
            count = std::max(count, res.transientAllocationId + 1);
    }
    return count;
}

static void fillTransientAllocationInfo(GpuGraph graph, uint32_t allocId,
                                        GpuGraphTransientAllocationInfo* outInfo)
{
    memset(outInfo, 0, sizeof(*outInfo));
    outInfo->allocationId = allocId;
    outInfo->firstUsePass = UINT32_MAX;
    outInfo->poolIndex = UINT32_MAX;

    for (auto& res : graph->resources) {
        if (res.imported || res.transientAllocationId != allocId) continue;
        outInfo->kind = res.kind;
        outInfo->resourceCount++;
        if (res.firstPassIndex < outInfo->firstUsePass) outInfo->firstUsePass = res.firstPassIndex;
        if (res.lastPassIndex > outInfo->lastUsePass) outInfo->lastUsePass = res.lastPassIndex;
        if (res.aliased) outInfo->aliased = true;
        if (res.kind == GPU_GRAPH_RESOURCE_TEXTURE) {
            outInfo->textureWidth = res.textureDesc.width;
            outInfo->textureHeight = res.textureDesc.height;
            outInfo->textureFormat = res.textureDesc.format;
            if (res.poolTextureIndex != UINT32_MAX) outInfo->poolIndex = res.poolTextureIndex;
        } else {
            outInfo->bufferSize = res.bufferDesc.size;
            if (res.poolBufferIndex != UINT32_MAX) outInfo->poolIndex = res.poolBufferIndex;
        }
    }
}

GpuResult gpuGraphGetTransientAllocation(GpuGraph graph, uint32_t allocationIndex,
                                         GpuGraphTransientAllocationInfo* outInfo)
{
    if (!graph || !outInfo || !graph->compiled) return GPU_ERROR_INVALID_ARGS;
    if (allocationIndex >= gpuGraphGetTransientAllocationCount(graph)) return GPU_ERROR_INVALID_ARGS;
    fillTransientAllocationInfo(graph, allocationIndex, outInfo);
    return GPU_SUCCESS;
}

uint32_t gpuGraphBuildTransientAllocationPlan(GpuGraph graph, GpuTransientAllocationPlan* outPlans,
                                            uint32_t maxPlans)
{
    if (!graph || !graph->compiled) return 0;
    uint32_t count = gpuGraphGetTransientAllocationCount(graph);
    uint32_t written = 0;
    for (uint32_t ai = 0; ai < count && written < maxPlans; ai++) {
        GpuGraphTransientAllocationInfo info = {};
        fillTransientAllocationInfo(graph, ai, &info);
        GpuTransientAllocationPlan& plan = outPlans[written++];
        plan.allocationId = info.allocationId;
        plan.kind = info.kind;
        plan.resourceCount = info.resourceCount;
        plan.firstUsePass = info.firstUsePass;
        plan.lastUsePass = info.lastUsePass;
        plan.sizeBytes = info.kind == GPU_GRAPH_RESOURCE_BUFFER ? info.bufferSize : 0;
        plan.poolIndex = info.poolIndex;
        plan.objectAliased = info.aliased;
        plan.heapPlaced = false;
        for (auto& res : graph->resources) {
            if (res.imported || res.transientAllocationId != ai) continue;
            if (res.heapPlaced) {
                plan.heapPlaced = true;
                break;
            }
        }
    }
    return written;
}

GpuResult gpuGraphGetPassGpuDurationMs(GpuGraph graph, uint32_t passIndex, float* outDurationMs)
{
    if (!graph || !outDurationMs || !graph->compiled) return GPU_ERROR_INVALID_ARGS;
    if (!graph->enablePassProfiling || !graph->timestampPool) return GPU_ERROR_INVALID_ARGS;
    if (passIndex >= graph->passProfileBeginQuery.size()) return GPU_ERROR_INVALID_ARGS;

    uint32_t beginQi = graph->passProfileBeginQuery[passIndex];
    uint32_t endQi = graph->passProfileEndQuery[passIndex];
    if (beginQi == UINT32_MAX || endQi == UINT32_MAX) return GPU_ERROR_INVALID_ARGS;

    uint64_t timestamps[2] = {};
    GpuResult result = GPU_ERROR_INTERNAL;
    for (int attempt = 0; attempt < 32; attempt++) {
        result = gpuQueryPoolGetResults(graph->timestampPool, beginQi, 2, timestamps);
        if (result == GPU_SUCCESS && timestamps[1] >= timestamps[0]) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (result != GPU_SUCCESS) return result;

    GpuCommandQueue queue = nullptr;
    if (gpuGetQueue(graph->device, GPU_QUEUE_TYPE_GRAPHICS, &queue) != GPU_SUCCESS)
        return GPU_ERROR_INTERNAL;

    uint64_t frequency = 0;
    if (gpuGetQueueTimestampFrequency(queue, &frequency) != GPU_SUCCESS || frequency == 0)
        return GPU_ERROR_INTERNAL;

    *outDurationMs = (float)((timestamps[1] - timestamps[0]) * 1000.0 / (double)frequency);
    return GPU_SUCCESS;
}

bool gpuDeviceSupportsIndependentQueues(GpuDevice device)
{
    if (!device) return false;
    GpuQueueInfo computeInfo = {};
    GpuQueueInfo transferInfo = {};
    if (gpuGetQueueInfo(device, GPU_QUEUE_TYPE_COMPUTE, &computeInfo) != GPU_SUCCESS) return false;
    if (gpuGetQueueInfo(device, GPU_QUEUE_TYPE_TRANSFER, &transferInfo) != GPU_SUCCESS) return false;
    return computeInfo.support == GPU_QUEUE_SUPPORT_DEDICATED &&
           transferInfo.support == GPU_QUEUE_SUPPORT_DEDICATED;
}

uint32_t gpuGraphGetValidationWarningCount(GpuGraph graph)
{
    return graph ? (uint32_t)graph->validationWarnings.size() : 0;
}

const char* gpuGraphGetValidationWarning(GpuGraph graph, uint32_t warningIndex)
{
    if (!graph || warningIndex >= graph->validationWarnings.size()) return "";
    return graph->validationWarnings[warningIndex].c_str();
}

static const char* resourceStateName(GpuResourceState state)
{
    switch (state) {
    case GPU_RESOURCE_STATE_UNDEFINED: return "Undefined";
    case GPU_RESOURCE_STATE_COPY_DEST: return "CopyDest";
    case GPU_RESOURCE_STATE_COPY_SOURCE: return "CopySource";
    case GPU_RESOURCE_STATE_SHADER_RESOURCE: return "ShaderResource";
    case GPU_RESOURCE_STATE_UNORDERED_ACCESS: return "UnorderedAccess";
    case GPU_RESOURCE_STATE_RENDER_TARGET: return "RenderTarget";
    case GPU_RESOURCE_STATE_DEPTH_WRITE: return "DepthWrite";
    case GPU_RESOURCE_STATE_DEPTH_READ: return "DepthRead";
    case GPU_RESOURCE_STATE_PRESENT: return "Present";
    default: return "Unknown";
    }
}

static const char* passKindName(GpuGraphPassKind kind)
{
    switch (kind) {
    case GPU_GRAPH_PASS_RENDER: return "render";
    case GPU_GRAPH_PASS_COMPUTE: return "compute";
    case GPU_GRAPH_PASS_COPY: return "copy";
    default: return "unknown";
    }
}

static const char* passQueueTypeName(GpuGraphPassKind kind)
{
    switch (kind) {
    case GPU_GRAPH_PASS_COMPUTE: return "compute";
    case GPU_GRAPH_PASS_COPY: return "transfer";
    default: return "graphics";
    }
}

static const char* queueTypeName(GpuQueueType type)
{
    switch (type) {
    case GPU_QUEUE_TYPE_COMPUTE: return "compute";
    case GPU_QUEUE_TYPE_TRANSFER: return "transfer";
    default: return "graphics";
    }
}

static std::string buildGraphJsonString(GpuGraph graph)
{
    std::ostringstream f;
    GpuGraphExecuteMode mode = graph->compiled ? graph->effectiveExecuteMode : graph->executeMode;
    f << "{\n  \"execute_mode\": ";
    switch (mode) {
    case GPU_GRAPH_EXECUTE_PASS_SUBMIT: f << "\"pass_submit\""; break;
    case GPU_GRAPH_EXECUTE_MULTI_QUEUE: f << "\"multi_queue\""; break;
    default: f << "\"single_command_buffer\""; break;
    }
    f << ",\n  \"passes\": [\n";
    for (size_t i = 0; i < graph->passes.size(); i++) {
        auto& p = *graph->passes[i];
        f << "    {\"index\": " << i
          << ", \"name\": \"" << p.name << "\""
          << ", \"kind\": \"" << passKindName(p.kind) << "\""
          << ", \"queue_type\": \"" << passQueueTypeName(p.kind) << "\""
          << ", \"culled\": " << (p.culled ? "true" : "false");
        if (p.culled && !p.culledReason.empty())
            f << ", \"culled_reason\": \"" << p.culledReason << "\"";
        f << ", \"dependencies\": [";
        for (size_t d = 0; d < p.dependencies.size(); d++) {
            if (d > 0) f << ", ";
            f << p.dependencies[d];
        }
        f << "], \"accesses\": [";
        for (size_t a = 0; a < p.accesses.size(); a++) {
            if (a > 0) f << ", ";
            f << "{\"resource\": " << p.accesses[a].resource << ", \"access\": ";
            switch (p.accesses[a].access) {
            case GPU_GRAPH_ACCESS_READ: f << "\"read\""; break;
            case GPU_GRAPH_ACCESS_WRITE: f << "\"write\""; break;
            case GPU_GRAPH_ACCESS_READ_WRITE: f << "\"readwrite\""; break;
            case GPU_GRAPH_ACCESS_PRESENT: f << "\"present\""; break;
            }
            f << "}";
        }
        f << "], \"barriers\": [";
        if (i < graph->passBarriers.size()) {
            auto& barriers = graph->passBarriers[i];
            for (size_t b = 0; b < barriers.size(); b++) {
                if (b > 0) f << ", ";
                auto& barrier = barriers[b];
                if (barrier.isGlobalBarrier) {
                    f << "{\"global_barrier\": true"
                      << ", \"dest_pass\": " << barrier.destPassIndex
                      << ", \"hazard\": \"" << gpuHazardKindName(barrier.hazardKind) << "\"}";
                    continue;
                }
                const char* resName = barrier.resourceIndex < graph->resources.size()
                    ? graph->resources[barrier.resourceIndex].name.c_str() : "";
                f << "{\"resource\": \"" << resName << "\""
                  << ", \"before\": \"" << resourceStateName(barrier.before) << "\""
                  << ", \"after\": \"" << resourceStateName(barrier.after) << "\""
                  << ", \"access\": " << barrier.access
                  << ", \"src_pass\": " << barrier.srcPassIndex
                  << ", \"dest_pass\": " << barrier.destPassIndex
                  << ", \"mip\": " << barrier.mipLevel
                  << ", \"layer\": " << barrier.arrayLayer
                  << ", \"hazard\": \"" << gpuHazardKindName(barrier.hazardKind) << "\"";
                if (barrier.queueOwnershipTransfer) {
                    f << ", \"queue_ownership_transfer\": true"
                      << ", \"src_queue\": \"" << queueTypeName(barrier.srcQueue) << "\""
                      << ", \"dest_queue\": \"" << queueTypeName(barrier.destQueue) << "\"";
                }
                f << "}";
            }
        }
        f << "]}";
        if (i + 1 < graph->passes.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n  \"resources\": [\n";
    for (size_t i = 0; i < graph->resources.size(); i++) {
        auto& r = graph->resources[i];
        f << "    {\"name\": \"" << r.name << "\""
          << ", \"kind\": " << (r.kind == GPU_GRAPH_RESOURCE_TEXTURE ? "\"texture\"" : "\"buffer\"")
          << ", \"imported\": " << (r.imported ? "true" : "false")
          << ", \"transient\": " << (r.imported ? "false" : "true");
        if (!r.imported && r.transientAllocationId != UINT32_MAX)
            f << ", \"allocation_id\": " << r.transientAllocationId;
        if (!r.imported && r.aliased)
            f << ", \"aliased\": true";
        if (!r.imported)
            f << ", \"heap_placed\": false";
        if (!r.imported && r.poolTextureIndex != UINT32_MAX)
            f << ", \"pool_texture_id\": " << r.poolTextureIndex;
        if (!r.imported && r.poolBufferIndex != UINT32_MAX)
            f << ", \"pool_buffer_id\": " << r.poolBufferIndex;
        f << ", \"first_use_pass\": " << (r.firstPassIndex == UINT32_MAX ? -1 : (int)r.firstPassIndex)
          << ", \"last_use_pass\": " << (int)r.lastPassIndex
          << "}";
        if (i + 1 < graph->resources.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n  \"execution_order\": [";
    for (size_t i = 0; i < graph->executionOrder.size(); i++) {
        if (i > 0) f << ", ";
        f << graph->executionOrder[i];
    }
    f << "],\n  \"validation_warnings\": [";
    for (size_t i = 0; i < graph->validationWarnings.size(); i++) {
        if (i > 0) f << ", ";
        f << "\"" << graph->validationWarnings[i] << "\"";
    }
    f << "]\n}\n";
    return f.str();
}

static std::string buildGraphDotString(GpuGraph graph)
{
    std::ostringstream f;
    f << "digraph RenderGraph {\n";
    f << "  rankdir=LR;\n";
    f << "  node [shape=box];\n\n";

    for (size_t i = 0; i < graph->resources.size(); i++) {
        auto& r = graph->resources[i];
        f << "  res" << i << " [shape=ellipse, label=\"" << r.name << "\"";
        if (r.imported) f << ", style=filled, fillcolor=lightyellow";
        else if (r.aliased) f << ", style=filled, fillcolor=lavender";
        else f << ", style=filled, fillcolor=lightblue";
        f << "];\n";
    }
    f << "\n";

    for (size_t i = 0; i < graph->passes.size(); i++) {
        auto& p = *graph->passes[i];
        f << "  pass" << i << " [label=\"" << p.name << "\\n(" << passKindName(p.kind) << ")\"";
        if (p.culled) f << ", style=dashed, fillcolor=lightgray, style=\"filled,dashed\"";
        else if (p.kind == GPU_GRAPH_PASS_COMPUTE) f << ", style=filled, fillcolor=orange";
        else if (p.kind == GPU_GRAPH_PASS_COPY) f << ", style=filled, fillcolor=lightgreen";
        f << "];\n";
    }
    f << "\n";

    for (size_t i = 0; i < graph->passes.size(); i++) {
        auto& p = *graph->passes[i];
        if (p.culled) continue;
        for (auto& acc : p.accesses) {
            uint32_t ri = acc.resource - 1;
            if (ri < graph->resources.size())
                f << "  pass" << i << " -> res" << ri << " [style=dashed];\n";
        }
        for (uint32_t dep : p.dependencies) {
            if (dep < graph->passes.size() && !graph->passes[dep]->culled)
                f << "  pass" << dep << " -> pass" << i << ";\n";
        }
    }
    f << "}\n";
    return f.str();
}

static GpuResult copyStringToBuffer(const std::string& text, char* outBuffer, size_t* inOutSize)
{
    if (!inOutSize) return GPU_ERROR_INVALID_ARGS;
    size_t required = text.size() + 1;
    if (!outBuffer || *inOutSize < required) {
        *inOutSize = required;
        return GPU_ERROR_BUFFER_TOO_SMALL;
    }
    memcpy(outBuffer, text.c_str(), required);
    *inOutSize = required;
    return GPU_SUCCESS;
}

GpuResult gpuGraphExportDot(GpuGraph graph, const char* path)
{
    if (!graph || !path) return GPU_ERROR_INVALID_ARGS;
    std::ofstream f(path);
    if (!f.is_open()) return GPU_ERROR_INTERNAL;
    f << buildGraphDotString(graph);
    return GPU_SUCCESS;
}

GpuResult gpuGraphExportJson(GpuGraph graph, const char* path)
{
    if (!graph || !path) return GPU_ERROR_INVALID_ARGS;
    std::ofstream f(path);
    if (!f.is_open()) return GPU_ERROR_INTERNAL;
    f << buildGraphJsonString(graph);
    return GPU_SUCCESS;
}

GpuResult gpuGraphExportDotString(GpuGraph graph, char* outBuffer, size_t* inOutSize)
{
    if (!graph) return GPU_ERROR_INVALID_ARGS;
    return copyStringToBuffer(buildGraphDotString(graph), outBuffer, inOutSize);
}

GpuResult gpuGraphExportJsonString(GpuGraph graph, char* outBuffer, size_t* inOutSize)
{
    if (!graph) return GPU_ERROR_INVALID_ARGS;
    return copyStringToBuffer(buildGraphJsonString(graph), outBuffer, inOutSize);
}

uint32_t gpuGetTransientTexturePoolCount(GpuDevice device)
{
    return device ? (uint32_t)device->pooledTransientTextures.size() : 0;
}

uint32_t gpuGetTransientBufferPoolCount(GpuDevice device)
{
    return device ? (uint32_t)device->pooledTransientBuffers.size() : 0;
}
