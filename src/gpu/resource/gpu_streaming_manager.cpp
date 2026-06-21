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
    uint64_t pendingBytes;
    std::vector<UploadRequest> pendingUploads;
    std::mutex mutex;
};

GpuResult gpuStreamingManagerCreate(GpuDevice device, uint64_t uploadHeapSize, GpuStreamingManager* outMgr)
{
    if (!device || !outMgr) return GPU_ERROR_INVALID_ARGS;
    GpuStreamingManager mgr = new GpuStreamingManager_t();
    mgr->device = device;
    mgr->uploadHeapSize = uploadHeapSize > 0 ? uploadHeapSize : 64ull * 1024ull * 1024ull;
    mgr->pendingBytes = 0;
    *outMgr = mgr;
    return GPU_SUCCESS;
}

GpuResult gpuStreamingRequest(GpuStreamingManager mgr, GpuTextureHandle texture,
                              uint32_t mipStart, uint32_t mipCount,
                              const void* data, uint64_t dataSize)
{
    if (!mgr || !gpuHandleIsValid(texture) || !data || dataSize == 0 || mipCount != 1) return GPU_ERROR_INVALID_ARGS;
    rhi::ITexture* rhiTex = mgr->device->texturePool.resolve(texture.index, texture.generation);
    if (!rhiTex) return GPU_ERROR_INVALID_ARGS;
    if (mipStart >= rhiTex->getDesc().mipCount) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(mgr->mutex);
    if (dataSize > mgr->uploadHeapSize || mgr->pendingBytes + dataSize > mgr->uploadHeapSize) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    UploadRequest req = {};
    req.texture = texture;
    req.mipStart = mipStart;
    req.mipCount = mipCount;
    req.data.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + dataSize);
    mgr->pendingBytes += dataSize;
    mgr->pendingUploads.push_back(std::move(req));
    return GPU_SUCCESS;
}

GpuResult gpuStreamingUpdate(GpuStreamingManager mgr)
{
    if (!mgr) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(mgr->mutex);

    rhi::ICommandQueue* queue = mgr->device->graphicsQueue;
    if (!queue) return GPU_ERROR_INTERNAL;

    for (auto& req : mgr->pendingUploads) {
        rhi::ITexture* rhiTex = mgr->device->texturePool.resolve(req.texture.index, req.texture.generation);
        if (!rhiTex) return GPU_ERROR_INVALID_ARGS;

        rhi::ComPtr<rhi::ICommandEncoder> encoder;
        if (SLANG_FAILED(queue->createCommandEncoder(encoder.writeRef()))) continue;

        encoder->setTextureState(rhiTex, rhi::ResourceState::CopyDestination);
        rhi::SubresourceData subData = {};
        subData.data = req.data.data();
        rhi::SubresourceRange range = {};
        range.mip = req.mipStart;
        range.mipCount = req.mipCount;
        range.layer = 0;
        range.layerCount = 1;
        const auto& sz = rhiTex->getDesc().size;
        uint32_t mipWidth = sz.width >> req.mipStart;
        uint32_t mipHeight = sz.height >> req.mipStart;
        uint32_t mipDepth = sz.depth >> req.mipStart;
        if (mipWidth == 0) mipWidth = 1;
        if (mipHeight == 0) mipHeight = 1;
        if (mipDepth == 0) mipDepth = 1;
        encoder->uploadTextureData(rhiTex, range, {0, 0, 0}, {mipWidth, mipHeight, mipDepth}, &subData, 1);
        encoder->setTextureState(rhiTex, rhi::ResourceState::ShaderResource);

        rhi::ComPtr<rhi::ICommandBuffer> cmdBuf;
        encoder->finish(cmdBuf.writeRef());
        rhi::ICommandBuffer* cmds[] = { cmdBuf.get() };
        rhi::SubmitDesc submit = {};
        submit.commandBuffers = cmds;
        submit.commandBufferCount = 1;
        queue->submit(submit);
        queue->waitOnHost();
        mgr->device->textureStates[req.texture.index] = GPU_RESOURCE_STATE_SHADER_RESOURCE;
    }
    mgr->pendingUploads.clear();
    mgr->pendingBytes = 0;
    return GPU_SUCCESS;
}

void gpuStreamingManagerDestroy(GpuStreamingManager mgr)
{
    if (!mgr) return;
    delete mgr;
}
