#include "gpu/resource/gpu_frame_context.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/sync/gpu_fence.h"
#include <vector>
#include <mutex>
#include <cstring>

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

enum { kMaxInFlightDefault = 3 };
static constexpr uint64_t kDefaultPageSize = 64 * 1024;

struct DeferredBuf { GpuBufferHandle handle; uint64_t fence; };
struct DeferredTex { GpuTextureHandle handle; uint64_t fence; };
struct DeferredView { GpuTextureHandle handle; uint64_t fence; };

struct UploadPage {
    rhi::ComPtr<rhi::IBuffer> buffer;
    uint64_t capacity;
    uint64_t used;
};

struct Readback {
    rhi::ComPtr<rhi::IBuffer> staging;
    uint64_t fence;
    void* mapped;
    uint64_t size;
    bool done;
};

struct GpuFrameContext_t {
    GpuDevice device;
    uint32_t maxInFlight;
    GpuFence frameFence;
    uint64_t frameIndex;
    uint64_t lastSubmit;

    std::vector<DeferredBuf>  deferBuf;
    std::vector<DeferredTex>  deferTex;
    std::vector<DeferredView> deferView;
    std::vector<UploadPage>   pages;
    std::vector<Readback>     readbacks;
    std::mutex mtx;
};

static void releaseBuf(GpuDevice dev, GpuBufferHandle h)
{
    rhi::IBuffer* p = dev->bufferPool.resolve(h.index, h.generation);
    if (!p) return;
    p->release();
    dev->bufferStates[h.index] = GPU_RESOURCE_STATE_UNDEFINED;
    dev->bufferPool.release(h.index, h.generation);
}

static void releaseTex(GpuDevice dev, GpuTextureHandle h)
{
    rhi::ITexture* p = dev->texturePool.resolve(h.index, h.generation);
    if (!p) return;
    p->release();
    dev->textureStates[h.index] = GPU_RESOURCE_STATE_UNDEFINED;
    dev->texturePool.release(h.index, h.generation);
}

static void releaseView(GpuDevice dev, GpuTextureHandle h)
{
    rhi::ITextureView* p = dev->textureViewPool.resolve(h.index, h.generation);
    if (!p) return;
    p->release();
    dev->textureViewPool.release(h.index, h.generation);
}

GpuResult gpuFrameContextCreate(GpuDevice device, uint32_t maxInFlight, GpuFrameContext* out)
{
    if (!device || !out) return GPU_ERROR_INVALID_ARGS;
    if (maxInFlight == 0) maxInFlight = kMaxInFlightDefault;

    auto* ctx = new GpuFrameContext_t{};
    ctx->device = device;
    ctx->maxInFlight = maxInFlight;
    ctx->frameIndex = 0;
    ctx->lastSubmit = 0;

    GpuResult r = gpuCreateFence(device, 0, &ctx->frameFence);
    if (r != GPU_SUCCESS) { delete ctx; return r; }

    *out = ctx;
    return GPU_SUCCESS;
}

void gpuFrameContextDestroy(GpuFrameContext ctx)
{
    if (!ctx) return;
    if (ctx->device && ctx->device->graphicsQueue)
        ctx->device->graphicsQueue->waitOnHost();

    for (auto& d : ctx->deferBuf)  releaseBuf(ctx->device, d.handle);
    for (auto& d : ctx->deferTex)  releaseTex(ctx->device, d.handle);
    for (auto& d : ctx->deferView) releaseView(ctx->device, d.handle);
    for (auto& p : ctx->pages)     { if (p.buffer) p.buffer->release(); }
    for (auto& r : ctx->readbacks) {
        if (r.staging && r.mapped)
            ctx->device->rhiDevice->unmapBuffer(r.staging);
        if (r.staging) r.staging->release();
    }

    gpuDestroyFence(ctx->device, ctx->frameFence);
    delete ctx;
}

GpuResult gpuFrameBegin(GpuFrameContext ctx)
{
    if (!ctx) return GPU_ERROR_INVALID_ARGS;

    ctx->frameIndex++;

    uint64_t completed = gpuFenceGetCurrentValue(ctx->frameFence);
    uint64_t waitVal = (ctx->lastSubmit >= ctx->maxInFlight)
        ? ctx->lastSubmit - ctx->maxInFlight + 1 : 0;

    if (completed < waitVal) {
        gpuFenceWait(ctx->device, ctx->frameFence, waitVal, 5000);
        completed = gpuFenceGetCurrentValue(ctx->frameFence);
    }

    std::lock_guard<std::mutex> lk(ctx->mtx);

    size_t w = 0;
    for (size_t i = 0; i < ctx->deferBuf.size(); i++) {
        if (ctx->deferBuf[i].fence <= completed) releaseBuf(ctx->device, ctx->deferBuf[i].handle);
        else { if (w != i) ctx->deferBuf[w] = ctx->deferBuf[i]; w++; }
    }
    ctx->deferBuf.resize(w);

    w = 0;
    for (size_t i = 0; i < ctx->deferTex.size(); i++) {
        if (ctx->deferTex[i].fence <= completed) releaseTex(ctx->device, ctx->deferTex[i].handle);
        else { if (w != i) ctx->deferTex[w] = ctx->deferTex[i]; w++; }
    }
    ctx->deferTex.resize(w);

    w = 0;
    for (size_t i = 0; i < ctx->deferView.size(); i++) {
        if (ctx->deferView[i].fence <= completed) releaseView(ctx->device, ctx->deferView[i].handle);
        else { if (w != i) ctx->deferView[w] = ctx->deferView[i]; w++; }
    }
    ctx->deferView.resize(w);

    for (auto& p : ctx->pages) p.used = 0;

    w = 0;
    for (size_t i = 0; i < ctx->readbacks.size(); i++) {
        auto& rr = ctx->readbacks[i];
        if (!rr.done && rr.fence <= completed) {
            void* ptr = nullptr;
            if (SLANG_SUCCEEDED(ctx->device->rhiDevice->mapBuffer(
                    rr.staging, rhi::CpuAccessMode::Read, &ptr))) {
                rr.mapped = ptr;
                rr.done = true;
            }
        }
        if (w != i) ctx->readbacks[w] = ctx->readbacks[i];
        w++;
    }
    ctx->readbacks.resize(w);

    return GPU_SUCCESS;
}

GpuResult gpuFrameEnd(GpuFrameContext ctx, GpuCommandQueue queue)
{
    if (!ctx || !queue) return GPU_ERROR_INVALID_ARGS;
    ctx->lastSubmit++;
    return gpuQueueSubmitWithFence(queue, 0, nullptr, ctx->frameFence, ctx->lastSubmit);
}

uint64_t gpuFrameGetIndex(GpuFrameContext ctx)
{
    return ctx ? ctx->frameIndex : 0;
}

uint64_t gpuFrameGetCompletedFenceValue(GpuFrameContext ctx)
{
    return ctx ? gpuFenceGetCurrentValue(ctx->frameFence) : 0;
}

GpuFence gpuFrameGetFence(GpuFrameContext ctx)
{
    return ctx ? ctx->frameFence : nullptr;
}

void gpuFrameDeferDestroyBuffer(GpuFrameContext ctx, GpuBufferHandle h)
{
    if (!ctx) return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->deferBuf.push_back({h, ctx->lastSubmit});
}

void gpuFrameDeferDestroyTexture(GpuFrameContext ctx, GpuTextureHandle h)
{
    if (!ctx) return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->deferTex.push_back({h, ctx->lastSubmit});
}

void gpuFrameDeferDestroyTextureView(GpuFrameContext ctx, GpuTextureHandle h)
{
    if (!ctx) return;
    std::lock_guard<std::mutex> lk(ctx->mtx);
    ctx->deferView.push_back({h, ctx->lastSubmit});
}

GpuResult gpuFrameUploadData(GpuFrameContext ctx, GpuBufferHandle dst,
                             uint64_t offset, uint64_t size, const void* data)
{
    if (!ctx || !data || size == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* rhiDst = ctx->device->bufferPool.resolve(dst.index, dst.generation);
    if (!rhiDst) return GPU_ERROR_INVALID_ARGS;

    void* mapped = nullptr;
    if (SLANG_SUCCEEDED(ctx->device->rhiDevice->mapBuffer(
            rhiDst, rhi::CpuAccessMode::Write, &mapped))) {
        memcpy(static_cast<uint8_t*>(mapped) + offset, data, size);
        ctx->device->rhiDevice->unmapBuffer(rhiDst);
        return GPU_SUCCESS;
    }

    std::lock_guard<std::mutex> lk(ctx->mtx);

    UploadPage* page = nullptr;
    for (auto& p : ctx->pages) {
        if (p.capacity - p.used >= size) { page = &p; break; }
    }

    if (!page) {
        uint64_t ps = (size > kDefaultPageSize) ? size : kDefaultPageSize;
        UploadPage np;
        np.capacity = ps;
        np.used = 0;

        rhi::BufferDesc sd = {};
        sd.size = ps;
        sd.usage = rhi::BufferUsage::CopySource;
        sd.memoryType = rhi::MemoryType::Upload;
        sd.label = "frame_upload";

        if (SLANG_FAILED(ctx->device->rhiDevice->createBuffer(sd, nullptr, np.buffer.writeRef())) ||
            !np.buffer) {
            rhi::ComPtr<rhi::ICommandEncoder> enc;
            if (SLANG_FAILED(ctx->device->graphicsQueue->createCommandEncoder(enc.writeRef())))
                return GPU_ERROR_INTERNAL;
            enc->uploadBufferData(rhiDst, offset, size, data);
            rhi::ComPtr<rhi::ICommandBuffer> cb;
            enc->finish(cb.writeRef());
            rhi::SubmitDesc s = {};
            rhi::ICommandBuffer* c = cb.get();
            s.commandBuffers = &c;
            s.commandBufferCount = 1;
            ctx->device->graphicsQueue->submit(s);
            return GPU_SUCCESS;
        }
        ctx->pages.push_back(std::move(np));
        page = &ctx->pages.back();
    }

    void* sp = nullptr;
    if (SLANG_FAILED(ctx->device->rhiDevice->mapBuffer(
            page->buffer, rhi::CpuAccessMode::Write, &sp)))
        return GPU_ERROR_INTERNAL;

    memcpy(static_cast<uint8_t*>(sp) + page->used, data, size);
    ctx->device->rhiDevice->unmapBuffer(page->buffer);

    rhi::ComPtr<rhi::ICommandEncoder> enc;
    if (SLANG_FAILED(ctx->device->graphicsQueue->createCommandEncoder(enc.writeRef())))
        return GPU_ERROR_INTERNAL;

    enc->copyBuffer(rhiDst, offset, page->buffer, page->used, size);

    rhi::ComPtr<rhi::ICommandBuffer> cb;
    enc->finish(cb.writeRef());

    rhi::SubmitDesc s = {};
    rhi::ICommandBuffer* c = cb.get();
    s.commandBuffers = &c;
    s.commandBufferCount = 1;
    ctx->device->graphicsQueue->submit(s);

    page->used += size;
    return GPU_SUCCESS;
}

GpuResult gpuFrameRequestReadback(GpuFrameContext ctx, GpuBufferHandle src,
                                  uint64_t offset, uint64_t size, void** outPtr)
{
    if (!ctx || !outPtr) return GPU_ERROR_INVALID_ARGS;
    *outPtr = nullptr;

    rhi::IBuffer* rhiSrc = ctx->device->bufferPool.resolve(src.index, src.generation);
    if (!rhiSrc) return GPU_ERROR_INVALID_ARGS;

    rhi::BufferDesc sd = {};
    sd.size = size;
    sd.usage = rhi::BufferUsage::CopyDestination;
    sd.memoryType = rhi::MemoryType::ReadBack;
    sd.label = "frame_readback";

    rhi::ComPtr<rhi::IBuffer> staging;
    if (SLANG_FAILED(ctx->device->rhiDevice->createBuffer(sd, nullptr, staging.writeRef())))
        return GPU_ERROR_INTERNAL;

    rhi::ComPtr<rhi::ICommandEncoder> enc;
    if (SLANG_FAILED(ctx->device->graphicsQueue->createCommandEncoder(enc.writeRef())))
        return GPU_ERROR_INTERNAL;

    enc->copyBuffer(staging, 0, rhiSrc, offset, size);

    rhi::ComPtr<rhi::ICommandBuffer> cb;
    enc->finish(cb.writeRef());

    rhi::SubmitDesc s = {};
    rhi::ICommandBuffer* c = cb.get();
    s.commandBuffers = &c;
    s.commandBufferCount = 1;
    ctx->device->graphicsQueue->submit(s);

    std::lock_guard<std::mutex> lk(ctx->mtx);
    Readback rr;
    rr.staging = staging;
    rr.fence = ctx->lastSubmit;
    rr.mapped = nullptr;
    rr.size = size;
    rr.done = false;
    ctx->readbacks.push_back(rr);
    return GPU_SUCCESS;
}

uint32_t gpuFrameGetInFlightCount(GpuFrameContext ctx)
{
    if (!ctx) return 0;
    uint64_t done = gpuFenceGetCurrentValue(ctx->frameFence);
    return (uint32_t)(ctx->lastSubmit > done ? ctx->lastSubmit - done : 0);
}
