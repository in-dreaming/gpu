#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_internal.h"
#include <vector>
#include <cstdlib>

GpuResult gpuCreateDevice(const GpuDeviceDesc* desc, GpuDevice* outDevice)
{
    if (!desc || !outDevice) return GPU_ERROR_INVALID_ARGS;

    rhi::DeviceDesc rhiDesc = {};
    rhiDesc.enableValidation = desc->enableDebugLayer;
    // Bindless descriptor handles require SM 6.6 on D3D12; let slang-rhi pick the highest supported profile.
    rhiDesc.slang.targetProfile = nullptr;
    rhiDesc.bindless.textureCount = 4096;
    rhiDesc.bindless.bufferCount = 4096;
    rhiDesc.bindless.samplerCount = 256;

    rhi::ComPtr<rhi::IDevice> rhiDevice;

    auto tryCreate = [&](rhi::DeviceType type) -> bool {
        rhiDesc.deviceType = type;
        return SLANG_SUCCEEDED(rhi::getRHI()->createDevice(rhiDesc, rhiDevice.writeRef()));
    };

    bool created = false;
    switch (desc->preferredBackend) {
    case GPU_BACKEND_D3D12:
        created = tryCreate(rhi::DeviceType::D3D12);
        break;
    case GPU_BACKEND_VULKAN:
        created = tryCreate(rhi::DeviceType::Vulkan);
        break;
    case GPU_BACKEND_D3D11:
        created = tryCreate(rhi::DeviceType::D3D11);
        break;
    case GPU_BACKEND_METAL:
        created = tryCreate(rhi::DeviceType::Metal);
        break;
    case GPU_BACKEND_CPU:
        created = tryCreate(rhi::DeviceType::CPU);
        break;
    case GPU_BACKEND_CUDA:
        created = tryCreate(rhi::DeviceType::CUDA);
        break;
    case GPU_BACKEND_WGPU:
        created = tryCreate(rhi::DeviceType::WGPU);
        break;
    case GPU_BACKEND_DEFAULT:
    default:
        created = tryCreate(rhi::DeviceType::Default);
        if (!created) created = tryCreate(rhi::DeviceType::Vulkan);
        if (!created) created = tryCreate(rhi::DeviceType::D3D12);
        break;
    }

    if (!created) return GPU_ERROR_DEVICE_LOST;

    GpuDevice device = new GpuDevice_t();
    device->rhiDevice = rhiDevice;

    rhi::ComPtr<rhi::ICommandQueue> queue;
    if (SLANG_FAILED(rhiDevice->getQueue(rhi::QueueType::Graphics, queue.writeRef())) || !queue) {
        rhiDevice = nullptr;
        return GPU_ERROR_DEVICE_LOST;
    }
    device->graphicsQueue = queue;
    
    // slang-rhi only has Graphics queue type - the same queue handles all operations
    // Set compute and transfer to point to the same graphics queue
    device->computeQueue = queue;
    device->transferQueue = queue;

    *outDevice = device;
    return GPU_SUCCESS;
}

void gpuDestroyDevice(GpuDevice device)
{
    if (!device) return;

    constexpr uint32_t poolCap = GpuHandlePool<int>::capacity();

    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->tensorPool.slots[i];
        if (slot.alive && slot.ptr) {
            GpuTensorData* data = slot.ptr;
            GpuTensorStorage* storage = data->storage;
            if (storage) {
                if (storage->refCount > 0) storage->refCount--;
                if (storage->refCount == 0) {
                    gpuDestroyBuffer(device, storage->bufferHandle);
                    free(storage);
                }
            }
            free(data);
            slot.ptr = nullptr;
            slot.alive = false;
        }
    }

    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->pipelinePool.slots[i];
        if (slot.alive && slot.ptr) { slot.ptr->release(); slot.ptr = nullptr; slot.alive = false; }
    }
    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->shaderObjectPool.slots[i];
        if (slot.alive && slot.ptr) { slot.ptr->release(); slot.ptr = nullptr; slot.alive = false; }
    }

    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->fencePool.slots[i];
        if (slot.alive && slot.ptr) { slot.ptr->release(); slot.ptr = nullptr; slot.alive = false; }
    }
    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->bufferPool.slots[i];
        if (slot.alive && slot.ptr) { slot.ptr->release(); slot.ptr = nullptr; slot.alive = false; }
    }

    gpuQueueWaitOnHost((GpuCommandQueue)device->graphicsQueue.get());

    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->textureViewPool.slots[i];
        if (slot.alive && slot.ptr) { slot.ptr->release(); slot.ptr = nullptr; slot.alive = false; }
    }
    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->texturePool.slots[i];
        if (slot.alive && slot.ptr) { slot.ptr->release(); slot.ptr = nullptr; slot.alive = false; }
    }
    for (uint32_t i = 1; i < poolCap; i++) {
        auto& slot = device->accelStructPool.slots[i];
        if (slot.alive && slot.ptr) { slot.ptr->release(); slot.ptr = nullptr; slot.alive = false; }
    }

    device->graphicsQueue = nullptr;
    device->rhiDevice = nullptr;
    delete device;
}

GpuResult gpuGetQueue(GpuDevice device, GpuQueueType type, GpuCommandQueue* outQueue)
{
    if (!device || !outQueue) return GPU_ERROR_INVALID_ARGS;
    
    switch (type) {
    case GPU_QUEUE_TYPE_GRAPHICS:
        *outQueue = (GpuCommandQueue)device->graphicsQueue.get();
        return GPU_SUCCESS;
    case GPU_QUEUE_TYPE_COMPUTE:
        if (!device->computeQueue) return GPU_ERROR_NOT_SUPPORTED;
        *outQueue = (GpuCommandQueue)device->computeQueue.get();
        return GPU_SUCCESS;
    case GPU_QUEUE_TYPE_TRANSFER:
        if (!device->transferQueue) return GPU_ERROR_NOT_SUPPORTED;
        *outQueue = (GpuCommandQueue)device->transferQueue.get();
        return GPU_SUCCESS;
    default:
        return GPU_ERROR_INVALID_ARGS;
    }
}

GpuCommandEncoder gpuBeginCommandEncoder(GpuDevice device, GpuCommandQueue queue)
{
    if (!device || !queue) return nullptr;

    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);
    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    if (SLANG_FAILED(rhiQueue->createCommandEncoder(encoder.writeRef()))) return nullptr;

    GpuCommandEncoder enc = new GpuCommandEncoder_t();
    enc->rhiEncoder = encoder;
    enc->queue = rhiQueue;
    enc->device = device;
    return enc;
}

static void finalizeCommandBuffer(GpuCommandBuffer_t* buf)
{
    if (!buf) return;
    if (buf->inRayTracingPass && buf->rtPassEncoder) {
        buf->rtPassEncoder->end();
        buf->inRayTracingPass = false;
        buf->rtPassEncoder = nullptr;
    }
    if (buf->inRenderPass && buf->renderPassEncoder) {
        buf->renderPassEncoder->end();
        buf->inRenderPass = false;
        buf->renderPassEncoder = nullptr;
    }
    if (buf->inComputePass && buf->computePassEncoder) {
        buf->computePassEncoder->end();
        buf->inComputePass = false;
        buf->computePassEncoder = nullptr;
    }
    if (buf->rhiEncoder) {
        rhi::ComPtr<rhi::ICommandBuffer> cmdBuffer;
        buf->rhiEncoder->finish(cmdBuffer.writeRef());
        buf->rhiCmdBuffer = cmdBuffer;
        buf->rhiEncoder = nullptr;
    }
}

GpuCommandBuffer gpuFinishCommandEncoder(GpuCommandEncoder encoder)
{
    if (!encoder) return nullptr;

    GpuCommandBuffer buf = new GpuCommandBuffer_t();
    buf->rhiEncoder = encoder->rhiEncoder;
    buf->device = encoder->device;
    buf->boundPipeline = {0, 0};
    buf->inComputePass = false;
    buf->computePassEncoder = nullptr;
    buf->inRenderPass = false;
    buf->renderPassEncoder = nullptr;
    buf->inRayTracingPass = false;
    buf->rtPassEncoder = nullptr;
    memset(buf->blendConstants, 0, sizeof(buf->blendConstants));
    buf->blendConstantsSet = false;
    buf->depthBiasConstant = 0.0f;
    buf->depthBiasSlopeScaled = 0.0f;
    buf->depthBiasClamp = 0.0f;
    buf->depthBiasSet = false;
    buf->rootShaderObject = nullptr;
    memset(buf->pendingBinds, 0, sizeof(buf->pendingBinds));
    buf->pendingBindCount = 0;

    delete encoder;
    return buf;
}

GpuResult gpuQueueSubmit(GpuCommandQueue queue, uint32_t count, GpuCommandBuffer* cmdBuffers)
{
    if (!queue || !cmdBuffers) return GPU_ERROR_INVALID_ARGS;

    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);

    std::vector<rhi::ICommandBuffer*> rhiCmdBufs;
    std::vector<GpuCommandBuffer_t*> toDelete;
    for (uint32_t i = 0; i < count; i++) {
        if (!cmdBuffers[i]) continue;
        GpuCommandBuffer_t* buf = static_cast<GpuCommandBuffer_t*>(cmdBuffers[i]);
        finalizeCommandBuffer(buf);
        if (buf->rhiCmdBuffer) {
            rhiCmdBufs.push_back(buf->rhiCmdBuffer);
        }
        toDelete.push_back(buf);
    }

    rhi::SubmitDesc submitDesc = {};
    submitDesc.commandBuffers = rhiCmdBufs.data();
    submitDesc.commandBufferCount = (uint32_t)rhiCmdBufs.size();
    rhiQueue->submit(submitDesc);

    for (auto* buf : toDelete) delete buf;
    return GPU_SUCCESS;
}

GpuResult gpuQueueWaitOnHost(GpuCommandQueue queue)
{
    if (!queue) return GPU_ERROR_INVALID_ARGS;
    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);
    return SLANG_SUCCEEDED(rhiQueue->waitOnHost()) ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}

GpuRenderPassEncoder gpuCmdBeginRenderPass(GpuCommandEncoder encoder, const GpuRenderPassDesc* desc)
{
    if (!encoder || !desc) return nullptr;
    // Need at least one color attachment or a depth attachment
    if (desc->colorAttachmentCount == 0 && !desc->depthAttachment) return nullptr;
    if (desc->colorAttachmentCount > 8) return nullptr;

    rhi::RenderPassColorAttachment colorAttachments[8];
    for (uint32_t i = 0; i < desc->colorAttachmentCount; i++) {
        auto& src = desc->colorAttachments[i];
        auto& dst = colorAttachments[i];

        if (src.attachment) {
            GpuSurfaceTexture_t* surfTex = static_cast<GpuSurfaceTexture_t*>(src.attachment);
            dst.view = surfTex->rhiTexture->getDefaultView();
        } else if (src.viewHandle.index != 0) {
            rhi::ITextureView* view = encoder->device->textureViewPool.resolve(src.viewHandle.index, src.viewHandle.generation);
            if (view) {
                dst.view = view;
            }
        } else if (src.textureHandle.index != 0) {
            rhi::ITexture* tex = encoder->device->texturePool.resolve(src.textureHandle.index, src.textureHandle.generation);
            if (tex) {
                dst.view = tex->getDefaultView();
            }
        }
        dst.loadOp = (rhi::LoadOp)src.loadOp;
        dst.storeOp = (rhi::StoreOp)src.storeOp;
        dst.clearValue[0] = src.clearValue[0];
        dst.clearValue[1] = src.clearValue[1];
        dst.clearValue[2] = src.clearValue[2];
        dst.clearValue[3] = src.clearValue[3];
    }

    rhi::RenderPassDepthStencilAttachment depthAtt;
    if (desc->depthAttachment) {
        auto& src = *desc->depthAttachment;
        depthAtt = {};
        if (src.viewHandle.index != 0) {
            rhi::ITextureView* view = encoder->device->textureViewPool.resolve(src.viewHandle.index, src.viewHandle.generation);
            if (view) depthAtt.view = view;
        } else if (src.textureHandle.index != 0) {
            rhi::ITexture* tex = encoder->device->texturePool.resolve(src.textureHandle.index, src.textureHandle.generation);
            if (tex) depthAtt.view = tex->getDefaultView();
        }
        depthAtt.depthLoadOp = (rhi::LoadOp)src.depthLoadOp;
        depthAtt.depthStoreOp = (rhi::StoreOp)src.depthStoreOp;
        depthAtt.depthClearValue = src.clearDepth;
        depthAtt.stencilLoadOp = (rhi::LoadOp)src.stencilLoadOp;
        depthAtt.stencilStoreOp = (rhi::StoreOp)src.stencilStoreOp;
        depthAtt.stencilClearValue = src.clearStencil;
    }

    rhi::RenderPassDesc rhiDesc = {};
    rhiDesc.colorAttachments = colorAttachments;
    rhiDesc.colorAttachmentCount = desc->colorAttachmentCount;
    if (desc->depthAttachment) {
        rhiDesc.depthStencilAttachment = &depthAtt;
    }

    auto* passEncoder = encoder->rhiEncoder->beginRenderPass(rhiDesc);
    if (!passEncoder) return nullptr;

    GpuRenderPassEncoder enc = new GpuRenderPassEncoder_t();
    enc->rhiPassEncoder = passEncoder;
    enc->device = encoder->device;
    return enc;
}

void gpuCmdEndRenderPass(GpuRenderPassEncoder pass)
{
    if (!pass) return;
    pass->rhiPassEncoder->end();
    delete pass;
}

void gpuCmdBindRenderPipeline(GpuRenderPassEncoder pass, GpuRenderPipeline pipeline)
{
    if (!pass || !pipeline) return;
    auto* rhiPipe = static_cast<GpuRenderPipeline_t*>(pipeline)->rhiPipeline.get();
    pass->rhiPassEncoder->bindPipeline(rhiPipe);
}

void gpuCmdSetViewport(GpuRenderPassEncoder pass, float x, float y, float width, float height)
{
    if (!pass) return;
    rhi::RenderState state = {};
    state.viewportCount = 1;
    state.viewports[0].originX = x;
    state.viewports[0].originY = y;
    state.viewports[0].extentX = width;
    state.viewports[0].extentY = height;
    state.viewports[0].minZ = 0.0f;
    state.viewports[0].maxZ = 1.0f;
    state.scissorRectCount = 1;
    state.scissorRects[0].minX = (int32_t)x;
    state.scissorRects[0].minY = (int32_t)y;
    state.scissorRects[0].maxX = (int32_t)(x + width);
    state.scissorRects[0].maxY = (int32_t)(y + height);
    pass->rhiPassEncoder->setRenderState(state);
}

void gpuCmdSetVertexBuffer(GpuRenderPassEncoder pass, uint32_t slot, GpuBufferHandle buffer, uint64_t offset)
{
    if (!pass || buffer.index == 0) return;
    rhi::IBuffer* rhiBuf = pass->device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!rhiBuf) return;

    rhi::RenderState state = {};
    state.vertexBuffers[slot] = rhi::BufferOffsetPair(rhiBuf, offset);
    state.vertexBufferCount = slot + 1;
    pass->rhiPassEncoder->setRenderState(state);
}

void gpuCmdDraw(GpuRenderPassEncoder pass, uint32_t vertexCount, uint32_t instanceCount,
                uint32_t startVertex, uint32_t startInstance)
{
    if (!pass) return;
    rhi::DrawArguments args = {};
    args.vertexCount = vertexCount;
    args.instanceCount = instanceCount;
    args.startVertexLocation = startVertex;
    args.startInstanceLocation = startInstance;
    pass->rhiPassEncoder->draw(args);
}

GpuComputePassEncoder gpuCmdBeginComputePass(GpuCommandEncoder encoder)
{
    if (!encoder) return nullptr;
    auto* passEncoder = encoder->rhiEncoder->beginComputePass();
    if (!passEncoder) return nullptr;
    return (GpuComputePassEncoder)passEncoder;
}

void gpuCmdEndComputePass(GpuComputePassEncoder pass)
{
    if (!pass) return;
    auto* rhiPass = reinterpret_cast<rhi::IComputePassEncoder*>(pass);
    rhiPass->end();
}

void gpuCmdBindComputePipeline(GpuComputePassEncoder pass, GpuComputePipeline pipeline)
{
    if (!pass || !pipeline) return;
    auto* rhiPipe = static_cast<GpuComputePipeline_t*>(pipeline)->rhiPipeline.get();
    auto* rhiPass = reinterpret_cast<rhi::IComputePassEncoder*>(pass);
    rhiPass->bindPipeline(rhiPipe);
}

void gpuCmdDispatchCompute(GpuComputePassEncoder pass, uint32_t x, uint32_t y, uint32_t z)
{
    if (!pass) return;
    auto* rhiPass = reinterpret_cast<rhi::IComputePassEncoder*>(pass);
    rhiPass->dispatchCompute(x, y, z);
}
