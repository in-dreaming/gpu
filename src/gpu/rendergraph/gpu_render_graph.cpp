#include "gpu/rendergraph/gpu_render_graph.h"
#include "gpu/core/gpu_internal.h"
#include <vector>
#include <string>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <fstream>
#include <memory>

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
};

struct GpuGraphPassAccess {
    GpuGraphResource resource;
    GpuGraphAccess access;
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
    std::vector<uint32_t> dependencies;
    uint32_t topoOrder;
};

struct GpuCompiledBarrier {
    bool isTexture;
    GpuTextureHandle tex;
    GpuBufferHandle buf;
    GpuResourceState before;
    GpuResourceState after;
};

struct GpuGraph_t {
    GpuDevice device;
    std::vector<GpuGraphResourceRecord> resources;
    std::vector<std::unique_ptr<GpuGraphPass_t>> passes;
    bool compiled;
    std::vector<std::vector<GpuCompiledBarrier>> passBarriers;
    std::vector<uint32_t> executionOrder;
};

GpuResult gpuGraphCreate(GpuDevice device, GpuGraph* outGraph)
{
    if (!device || !outGraph) return GPU_ERROR_INVALID_ARGS;
    auto* g = new GpuGraph_t{};
    g->device = device;
    g->compiled = false;
    *outGraph = g;
    return GPU_SUCCESS;
}

void gpuGraphDestroy(GpuGraph graph)
{
    if (!graph) return;
    if (graph->compiled) {
        for (auto& res : graph->resources) {
            if (res.kind == GPU_GRAPH_RESOURCE_TEXTURE) {
                if (!res.imported) {
                    if (res.realizedView.index != 0) gpuDestroyTextureView(graph->device, res.realizedView);
                    if (res.depthView.index != 0) gpuDestroyTextureView(graph->device, res.depthView);
                    if (res.realizedTexture.index != 0) gpuDestroyTexture(graph->device, res.realizedTexture);
                } else {
                    if (res.depthView.index != 0) gpuDestroyTextureView(graph->device, res.depthView);
                    if (!res.isSurfaceTexture && res.realizedView.index != 0)
                        gpuDestroyTextureView(graph->device, res.realizedView);
                }
            } else {
                if (!res.imported && res.realizedBuffer.index != 0) gpuDestroyBuffer(graph->device, res.realizedBuffer);
            }
        }
    }
    delete graph;
}

void gpuGraphReset(GpuGraph graph)
{
    if (!graph) return;
    if (graph->compiled) {
        for (auto& res : graph->resources) {
            if (res.kind == GPU_GRAPH_RESOURCE_TEXTURE) {
                if (!res.imported) {
                    if (res.realizedView.index != 0) gpuDestroyTextureView(graph->device, res.realizedView);
                    if (res.depthView.index != 0) gpuDestroyTextureView(graph->device, res.depthView);
                    if (res.realizedTexture.index != 0) gpuDestroyTexture(graph->device, res.realizedTexture);
                } else {
                    if (res.depthView.index != 0) gpuDestroyTextureView(graph->device, res.depthView);
                    if (!res.isSurfaceTexture && res.realizedView.index != 0)
                        gpuDestroyTextureView(graph->device, res.realizedView);
                }
            } else {
                if (!res.imported && res.realizedBuffer.index != 0) gpuDestroyBuffer(graph->device, res.realizedBuffer);
            }
        }
    }
    graph->resources.clear();
    graph->passes.clear();
    graph->compiled = false;
    graph->passBarriers.clear();
    graph->executionOrder.clear();
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
    graph->passes.emplace_back(p);
    return p;
}

static void addAccess(GpuGraphPass pass, GpuGraphResource resource, GpuGraphAccess access)
{
    if (!pass || resource == GPU_GRAPH_NULL_RESOURCE) return;
    for (auto& a : pass->accesses) {
        if (a.resource == resource) {
            if (access == GPU_GRAPH_ACCESS_WRITE || access == GPU_GRAPH_ACCESS_READ_WRITE)
                a.access = access;
            return;
        }
    }
    pass->accesses.push_back({resource, access});
}

void gpuGraphPassRead(GpuGraphPass pass, GpuGraphResource resource) { addAccess(pass, resource, GPU_GRAPH_ACCESS_READ); }
void gpuGraphPassWrite(GpuGraphPass pass, GpuGraphResource resource) { addAccess(pass, resource, GPU_GRAPH_ACCESS_WRITE); }
void gpuGraphPassReadWrite(GpuGraphPass pass, GpuGraphResource resource) { addAccess(pass, resource, GPU_GRAPH_ACCESS_READ_WRITE); }

void gpuGraphPassSetColorAttachments(GpuGraphPass pass, uint32_t count, const GpuGraphColorAttachment* attachments)
{
    if (!pass || !attachments) return;
    pass->colorAttachments.assign(attachments, attachments + count);
    for (uint32_t i = 0; i < count; i++)
        addAccess(pass, attachments[i].resource, GPU_GRAPH_ACCESS_WRITE);
}

void gpuGraphPassSetDepthAttachment(GpuGraphPass pass, const GpuGraphDepthAttachment* attachment)
{
    if (!pass || !attachment) return;
    pass->depthAttachment = *attachment;
    pass->hasDepth = true;
    addAccess(pass, attachment->resource, GPU_GRAPH_ACCESS_WRITE);
}

void gpuGraphPassSetCallback(GpuGraphPass pass, GpuGraphPassCallback callback, void* userData)
{
    if (!pass) return;
    pass->callback = callback;
    pass->userData = userData;
}

static GpuResourceState accessToState(GpuGraphAccess access, GpuGraphResourceKind /*kind*/,
                                       GpuGraphPassKind passKind, bool isColorAttachment, bool isDepthAttachment)
{
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
            if (acc.access == GPU_GRAPH_ACCESS_WRITE || acc.access == GPU_GRAPH_ACCESS_READ_WRITE)
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
                if (acc.access == GPU_GRAPH_ACCESS_WRITE || acc.access == GPU_GRAPH_ACCESS_READ_WRITE) {
                    if (graph->resources[ri].imported) needed = true;
                    resourceNeeded[ri] = true;
                }
            }
            if (pass.callback && iter == 0) needed = true;
            if (!needed && !pass.dependencies.empty()) needed = true;
            if (!needed) pass.culled = true;
        }
    }
    for (auto& p : graph->passes) {
        if (p->callback) p->culled = false;
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
            GpuResult r = gpuCreateTexture(graph->device, &res.textureDesc, &res.realizedTexture);
            if (r != GPU_SUCCESS) return r;
            if (res.textureDesc.usage & GPU_TEXTURE_USAGE_RENDER_TARGET) {
                r = gpuCreateTextureView(graph->device, res.realizedTexture,
                    GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &res.realizedView);
                if (r != GPU_SUCCESS) return r;
            }
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
            GpuResult r = gpuCreateBuffer(graph->device, &res.bufferDesc, &res.realizedBuffer);
            if (r != GPU_SUCCESS) return r;
        }
    }

    graph->passBarriers.resize(passCount);
    for (auto& res : graph->resources) res.currentState = res.initialState;

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
            GpuResourceState targetState = GPU_RESOURCE_STATE_RENDER_TARGET;
            if (res.currentState != targetState) {
                GpuCompiledBarrier b;
                b.isTexture = true;
                b.tex = res.imported ? res.importedTexture : res.realizedTexture;
                b.before = res.currentState;
                b.after = targetState;
                barriers.push_back(b);
                res.currentState = targetState;
            }
        }

        if (pass.hasDepth) {
            uint32_t ri = pass.depthAttachment.resource - 1;
            if (ri < resCount) {
                auto& res = graph->resources[ri];
                GpuResourceState targetState = GPU_RESOURCE_STATE_DEPTH_WRITE;
                if (res.currentState != targetState) {
                    GpuCompiledBarrier b;
                    b.isTexture = true;
                    b.tex = res.imported ? res.importedTexture : res.realizedTexture;
                    b.before = res.currentState;
                    b.after = targetState;
                    barriers.push_back(b);
                    res.currentState = targetState;
                }
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

            GpuResourceState targetState = accessToState(acc.access, res.kind, pass.kind, false, false);
            if (res.currentState != targetState) {
                GpuCompiledBarrier b;
                b.isTexture = (res.kind == GPU_GRAPH_RESOURCE_TEXTURE);
                if (b.isTexture) b.tex = res.imported ? res.importedTexture : res.realizedTexture;
                else b.buf = res.imported ? res.importedBuffer : res.realizedBuffer;
                b.before = res.currentState;
                b.after = targetState;
                barriers.push_back(b);
                res.currentState = targetState;
            }
        }
    }

    for (auto& res : graph->resources) {
        if (res.imported && res.initialState != res.currentState) {
            if (!graph->executionOrder.empty()) {
                uint32_t lastPi = graph->executionOrder.back();
                GpuCompiledBarrier b;
                b.isTexture = (res.kind == GPU_GRAPH_RESOURCE_TEXTURE);
                if (b.isTexture) b.tex = res.importedTexture;
                else b.buf = res.importedBuffer;
                b.before = res.currentState;
                b.after = res.initialState;
                graph->passBarriers[lastPi].push_back(b);
                res.currentState = res.initialState;
            }
        }
    }

    graph->compiled = true;
    return GPU_SUCCESS;
}

static void emitBarriers(GpuGraph graph, GpuCommandEncoder encoder, const std::vector<GpuCompiledBarrier>& barriers)
{
    for (auto& b : barriers) {
        if (b.isTexture && b.tex.index != 0)
            gpuCmdSetTextureState(graph->device, encoder, b.tex, b.after);
        else if (!b.isTexture && b.buf.index != 0)
            gpuCmdSetBufferState(graph->device, encoder, b.buf, b.after);
    }
}

GpuResult gpuGraphExecute(GpuGraph graph, GpuCommandQueue queue)
{
    if (!graph || !queue) return GPU_ERROR_INVALID_ARGS;
    if (!graph->compiled) return GPU_ERROR_INVALID_ARGS;

    GpuCommandEncoder encoder = gpuBeginCommandEncoder(graph->device, queue);
    if (!encoder) return GPU_ERROR_INTERNAL;

    for (uint32_t si = 0; si < (uint32_t)graph->executionOrder.size(); si++) {
        uint32_t pi = graph->executionOrder[si];
        auto& pass = *graph->passes[pi];
        if (pass.culled) continue;

        emitBarriers(graph, encoder, graph->passBarriers[pi]);

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
                    // Create depth-stencil view if needed
                    if (res.depthView.index == 0) {
                        GpuTextureHandle texH = res.imported ? res.importedTexture : res.realizedTexture;
                        gpuCreateTextureView(graph->device, texH, GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL, &res.depthView);
                    }
                    depthAtt.viewHandle = res.depthView;
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
            if (pass.callback) pass.callback(&ctx, pass.userData);
        }
    }

    GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
    if (cmd) {
        gpuQueueSubmit(queue, 1, &cmd);
    }
    return GPU_SUCCESS;
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

GpuResult gpuGraphExportDot(GpuGraph graph, const char* path)
{
    if (!graph || !path) return GPU_ERROR_INVALID_ARGS;
    std::ofstream f(path);
    if (!f.is_open()) return GPU_ERROR_INTERNAL;
    f << "digraph RenderGraph {\n";
    f << "  rankdir=LR;\n";
    f << "  node [shape=box];\n\n";
    for (size_t i = 0; i < graph->passes.size(); i++) {
        auto& p = *graph->passes[i];
        if (p.culled) continue;
        f << "  pass" << i << " [label=\"" << p.name << "\"];\n";
    }
    f << "\n";
    for (size_t i = 0; i < graph->passes.size(); i++) {
        auto& p = *graph->passes[i];
        if (p.culled) continue;
        for (uint32_t dep : p.dependencies) {
            if (!graph->passes[dep]->culled)
                f << "  pass" << dep << " -> pass" << i << ";\n";
        }
    }
    f << "}\n";
    return GPU_SUCCESS;
}

GpuResult gpuGraphExportJson(GpuGraph graph, const char* path)
{
    if (!graph || !path) return GPU_ERROR_INVALID_ARGS;
    std::ofstream f(path);
    if (!f.is_open()) return GPU_ERROR_INTERNAL;
    f << "{\n  \"passes\": [\n";
    for (size_t i = 0; i < graph->passes.size(); i++) {
        auto& p = *graph->passes[i];
        f << "    {\"name\": \"" << p.name << "\", \"kind\": ";
        switch (p.kind) {
        case GPU_GRAPH_PASS_RENDER: f << "\"render\""; break;
        case GPU_GRAPH_PASS_COMPUTE: f << "\"compute\""; break;
        case GPU_GRAPH_PASS_COPY: f << "\"copy\""; break;
        }
        f << ", \"culled\": " << (p.culled ? "true" : "false");
        f << ", \"dependencies\": [";
        for (size_t d = 0; d < p.dependencies.size(); d++) {
            if (d > 0) f << ", ";
            f << p.dependencies[d];
        }
        f << "], \"accesses\": [";
        for (size_t a = 0; a < p.accesses.size(); a++) {
            if (a > 0) f << ", ";
            f << "{\"resource\": " << p.accesses[a].resource;
            f << ", \"access\": ";
            switch (p.accesses[a].access) {
            case GPU_GRAPH_ACCESS_READ: f << "\"read\""; break;
            case GPU_GRAPH_ACCESS_WRITE: f << "\"write\""; break;
            case GPU_GRAPH_ACCESS_READ_WRITE: f << "\"readwrite\""; break;
            }
            f << "}";
        }
        f << "]}";
        if (i + 1 < graph->passes.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n  \"resources\": [\n";
    for (size_t i = 0; i < graph->resources.size(); i++) {
        auto& r = graph->resources[i];
        f << "    {\"name\": \"" << r.name << "\", \"kind\": ";
        f << (r.kind == GPU_GRAPH_RESOURCE_TEXTURE ? "\"texture\"" : "\"buffer\"");
        f << ", \"imported\": " << (r.imported ? "true" : "false");
        f << "}";
        if (i + 1 < graph->resources.size()) f << ",";
        f << "\n";
    }
    f << "  ],\n  \"execution_order\": [";
    for (size_t i = 0; i < graph->executionOrder.size(); i++) {
        if (i > 0) f << ", ";
        f << graph->executionOrder[i];
    }
    f << "]\n}\n";
    return GPU_SUCCESS;
}
