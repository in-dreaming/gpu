#include "gpu/resource/gpu_streaming_manager.h"
#include "gpu/core/gpu_internal.h"
#include <vector>
#include <mutex>

struct UploadRequest {
    GpuTextureHandle texture;
    uint32_t mipStart;
    uint32_t mipCount;
    std::vector<uint8_t> data;
};

struct GpuStreamingManager_t {
    GpuDevice device;
    uint64_t uploadHeapSize;
    std::vector<UploadRequest> pendingUploads;
    std::mutex mutex;
};

GpuResult gpuStreamingManagerCreate(GpuDevice device, uint64_t uploadHeapSize, GpuStreamingManager* outMgr)
{
    if (!device || !outMgr) return GPU_ERROR_INVALID_ARGS;
    GpuStreamingManager mgr = new GpuStreamingManager_t();
    mgr->device = device;
    mgr->uploadHeapSize = uploadHeapSize;
    *outMgr = mgr;
    return GPU_SUCCESS;
}

GpuResult gpuStreamingRequest(GpuStreamingManager mgr, GpuTextureHandle texture,
                              uint32_t mipStart, uint32_t mipCount,
                              const void* data, uint64_t dataSize)
{
    if (!mgr || !data) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(mgr->mutex);
    UploadRequest req = {};
    req.texture = texture;
    req.mipStart = mipStart;
    req.mipCount = mipCount;
    req.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + dataSize);
    mgr->pendingUploads.push_back(std::move(req));
    return GPU_SUCCESS;
}

GpuResult gpuStreamingUpdate(GpuStreamingManager mgr)
{
    if (!mgr) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(mgr->mutex);

    rhi::ComPtr<rhi::ICommandQueue> queue;
    if (SLANG_FAILED(mgr->device->rhiDevice->getQueue(rhi::QueueType::Graphics, queue.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    for (auto& req : mgr->pendingUploads) {
        rhi::ITexture* rhiTex = mgr->device->texturePool.resolve(req.texture.index, req.texture.generation);
        if (!rhiTex) continue;

        rhi::ComPtr<rhi::ICommandEncoder> encoder;
        if (SLANG_FAILED(queue->createCommandEncoder(encoder.writeRef()))) continue;

        rhi::SubresourceData subData = {};
        subData.data = req.data.data();
        rhi::SubresourceRange range = {};
        range.mip = req.mipStart;
        range.mipCount = req.mipCount;
        range.layer = 0;
        range.layerCount = 1;
        const auto& sz = rhiTex->getDesc().size;
        encoder->uploadTextureData(rhiTex, range, {0, 0, 0}, {sz.width, sz.height, sz.depth}, &subData, 1);

        rhi::ComPtr<rhi::ICommandBuffer> cmdBuf;
        encoder->finish(cmdBuf.writeRef());
        rhi::ICommandBuffer* cmds[] = { cmdBuf.get() };
        rhi::SubmitDesc submit = {};
        submit.commandBuffers = cmds;
        submit.commandBufferCount = 1;
        queue->submit(submit);
    }
    mgr->pendingUploads.clear();
    return GPU_SUCCESS;
}

void gpuStreamingManagerDestroy(GpuStreamingManager mgr)
{
    if (!mgr) return;
    delete mgr;
}
