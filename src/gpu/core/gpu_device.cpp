#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_backend.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <vector>
#include <cstdlib>

GpuResult gpuCreateDevice(const GpuDeviceDesc* desc, GpuDevice* outDevice)
{
    if (!desc || !outDevice) return GPU_ERROR_INVALID_ARGS;

    GpuDevice device = new GpuDevice_t();
    device->rhiDebugCallback = std::make_unique<GpuRhiDebugCallback>(device);

    if (desc->enableDebugLayer && !rhi::getRHI()->isDebugLayersEnabled()) {
        rhi::DebugLayerOptions debugOptions = {};
        debugOptions.required = true;
        debugOptions.coreValidation = true;
        if (SLANG_FAILED(rhi::getRHI()->setDebugLayerOptions(debugOptions))) {
            delete device;
            return GPU_ERROR_NOT_SUPPORTED;
        }
    }

    rhi::DeviceDesc rhiDesc = {};
    rhiDesc.enableValidation = desc->enableDebugLayer;
    rhiDesc.debugCallback = device->rhiDebugCallback.get();
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

    if (!created) {
        delete device;
        return GPU_ERROR_DEVICE_LOST;
    }

    device->rhiDevice = rhiDevice;

    rhi::ComPtr<rhi::ICommandQueue> queue;
    if (SLANG_FAILED(rhiDevice->getQueue(rhi::QueueType::Graphics, queue.writeRef())) || !queue) {
        delete device;
        return GPU_ERROR_DEVICE_LOST;
    }
    device->graphicsQueue = queue;

    auto assignQueue = [&](rhi::QueueType type, rhi::ComPtr<rhi::ICommandQueue>& outQueue, bool& outIsAlias) {
        rhi::ComPtr<rhi::ICommandQueue> q;
        if (SLANG_SUCCEEDED(rhiDevice->getQueue(type, q.writeRef())) && q) {
            outQueue = q;
            outIsAlias = (q.get() == device->graphicsQueue.get());
        } else {
            outQueue = device->graphicsQueue;
            outIsAlias = true;
        }
    };

    assignQueue(rhi::QueueType::Compute, device->computeQueue, device->computeQueueIsAlias);
    assignQueue(rhi::QueueType::Transfer, device->transferQueue, device->transferQueueIsAlias);

    *outDevice = device;
    return GPU_SUCCESS;
}

const char* gpuGetDeviceLastDiagnostic(GpuDevice device)
{
    return device ? device->lastError.c_str() : "invalid GPU device";
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

    for (auto& entry : device->pooledTransientTextures) {
        if (entry.rtView.index != 0) gpuDestroyTextureView(device, entry.rtView);
        if (entry.texture.index != 0) gpuDestroyTexture(device, entry.texture);
    }
    device->pooledTransientTextures.clear();
    for (auto& entry : device->pooledTransientBuffers) {
        if (entry.buffer.index != 0) gpuDestroyBuffer(device, entry.buffer);
    }
    device->pooledTransientBuffers.clear();

    device->graphicsQueue = nullptr;
    device->computeQueue = nullptr;
    device->transferQueue = nullptr;
    device->rhiDevice = nullptr;
    device->rhiDebugCallback = nullptr;
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

GpuResult gpuGetQueueInfo(GpuDevice device, GpuQueueType type, GpuQueueInfo* outInfo)
{
    if (!device || !outInfo) return GPU_ERROR_INVALID_ARGS;

    outInfo->type = type;
    outInfo->familyIndex = 0;
    outInfo->reason = "";

    switch (type) {
    case GPU_QUEUE_TYPE_GRAPHICS:
        if (!device->graphicsQueue) {
            outInfo->support = GPU_QUEUE_SUPPORT_UNAVAILABLE;
            outInfo->reason = "Graphics queue unavailable";
        } else {
            outInfo->support = GPU_QUEUE_SUPPORT_DEDICATED;
            outInfo->reason = "Dedicated graphics queue";
        }
        break;
    case GPU_QUEUE_TYPE_COMPUTE:
        if (!device->computeQueue) {
            outInfo->support = GPU_QUEUE_SUPPORT_UNAVAILABLE;
            outInfo->reason = "Compute queue unavailable";
        } else if (device->computeQueueIsAlias) {
            outInfo->support = GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS;
            switch (gpuGetBackendType(device)) {
            case GPU_BACKEND_WGPU:
                outInfo->reason = "WebGPU exposes a single queue";
                break;
            case GPU_BACKEND_D3D11:
                outInfo->reason = "D3D11 immediate context aliases all queue types";
                break;
            case GPU_BACKEND_CPU:
                outInfo->reason = "CPU backend serializes all queue types";
                break;
            case GPU_BACKEND_CUDA:
                outInfo->reason = "CUDA backend uses one stream for all queue types";
                break;
            default:
                outInfo->reason = "Compute queue aliases graphics queue (no independent async compute)";
                break;
            }
        } else {
            outInfo->support = GPU_QUEUE_SUPPORT_DEDICATED;
            outInfo->reason = "Dedicated compute queue";
        }
        break;
    case GPU_QUEUE_TYPE_TRANSFER:
        if (!device->transferQueue) {
            outInfo->support = GPU_QUEUE_SUPPORT_UNAVAILABLE;
            outInfo->reason = "Transfer queue unavailable";
        } else if (device->transferQueueIsAlias) {
            outInfo->support = GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS;
            switch (gpuGetBackendType(device)) {
            case GPU_BACKEND_WGPU:
                outInfo->reason = "WebGPU exposes a single queue";
                break;
            case GPU_BACKEND_D3D11:
                outInfo->reason = "D3D11 immediate context aliases all queue types";
                break;
            case GPU_BACKEND_CPU:
                outInfo->reason = "CPU backend serializes all queue types";
                break;
            case GPU_BACKEND_CUDA:
                outInfo->reason = "CUDA backend uses one stream for all queue types";
                break;
            default:
                outInfo->reason = "Transfer queue aliases graphics queue (no independent async transfer)";
                break;
            }
        } else {
            outInfo->support = GPU_QUEUE_SUPPORT_DEDICATED;
            outInfo->reason = "Dedicated transfer queue";
        }
        break;
    default:
        return GPU_ERROR_INVALID_ARGS;
    }

    return GPU_SUCCESS;
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

void gpuFinalizeCommandBuffer(GpuCommandBuffer_t* buf)
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

void gpuCancelCommandEncoder(GpuCommandEncoder encoder)
{
    if (!encoder) return;
    delete encoder;
}

void gpuDestroyCommandBuffer(GpuCommandBuffer commands)
{
    if (!commands) return;
    delete commands;
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
        gpuFinalizeCommandBuffer(buf);
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
    pass->rootShaderObject = pass->rhiPassEncoder->bindPipeline(rhiPipe);
}

void gpuCmdSetViewport(GpuRenderPassEncoder pass, float x, float y, float width, float height)
{
    if (!pass) return;
    rhi::RenderState& state = pass->renderState;
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

    rhi::RenderState& state = pass->renderState;
    state.vertexBuffers[slot] = rhi::BufferOffsetPair(rhiBuf, offset);
    if (state.vertexBufferCount < slot + 1) state.vertexBufferCount = slot + 1;
    pass->rhiPassEncoder->setRenderState(state);
}

extern rhi::IRenderPipeline* gpuResolveRenderPipeline(GpuDevice device, GpuPipelineHandle pipeline);

GpuResult gpuCmdBindGraphicsPipeline(GpuRenderPassEncoder pass, GpuPipelineHandle pipeline)
{
    if (!pass || !gpuHandleIsValid(pipeline)) return GPU_ERROR_INVALID_ARGS;
    rhi::IRenderPipeline* resolved = gpuResolveRenderPipeline(pass->device, pipeline);
    if (!resolved) return GPU_ERROR_INVALID_ARGS;
    pass->rootShaderObject = pass->rhiPassEncoder->bindPipeline(resolved);
    return pass->rootShaderObject ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}

GpuResult gpuCmdSetIndexBuffer(GpuRenderPassEncoder pass, GpuBufferHandle buffer, uint64_t offset, uint8_t index32)
{
    if (!pass || !gpuHandleIsValid(buffer)) return GPU_ERROR_INVALID_ARGS;
    rhi::IBuffer* resolved = pass->device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!resolved) return GPU_ERROR_INVALID_ARGS;
    rhi::RenderState& state = pass->renderState;
    state.indexBuffer = rhi::BufferOffsetPair(resolved, offset);
    state.indexFormat = index32 ? rhi::IndexFormat::Uint32 : rhi::IndexFormat::Uint16;
    pass->rhiPassEncoder->setRenderState(state);
    return GPU_SUCCESS;
}

static rhi::ShaderCursor bindingCursor(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding)
{
    if (!pass || !pass->rootShaderObject) return {};
    rhi::ShaderCursor cursor(pass->rootShaderObject);
    slang::TypeLayoutReflection* layout = cursor.getTypeLayout();
    if (!layout || layout->getKind() != slang::TypeReflection::Kind::Struct) return {};
    const SlangInt count = layout->getFieldCount();
    for (SlangInt index = 0; index < count; ++index) {
        slang::VariableLayoutReflection* field = layout->getFieldByIndex((unsigned int)index);
        if (field && field->getBindingSpace() == set && field->getBindingIndex() == binding)
            return cursor[(uint32_t)index];
    }
    return {};
}

GpuResult gpuCmdSetBindingData(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding, const void* data, size_t size)
{
    if (!data || size == 0) return GPU_ERROR_INVALID_ARGS;
    rhi::ShaderCursor cursor = bindingCursor(pass, set, binding);
    if (!cursor.isValid()) return GPU_ERROR_INVALID_ARGS;
    slang::TypeLayoutReflection* type = cursor.getTypeLayout();
    if (type && (type->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
                 type->getKind() == slang::TypeReflection::Kind::ParameterBlock)) {
        cursor = cursor.getDereferenced();
        if (!cursor.isValid()) return GPU_ERROR_INTERNAL;
    }
    return SLANG_SUCCEEDED(cursor.setData(data, size)) ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}

GpuResult gpuCmdSetBindingBuffer(
    GpuRenderPassEncoder pass,
    uint32_t set,
    uint32_t binding,
    GpuBufferHandle buffer,
    uint64_t offset,
    uint64_t range,
    GpuBufferBindingAccess access)
{
    if (!pass || !gpuHandleIsValid(buffer) || access > GPU_BUFFER_BINDING_CONSTANT) {
        return GPU_ERROR_INVALID_ARGS;
    }
    rhi::IBuffer* resolved = pass->device->bufferPool.resolve(buffer.index, buffer.generation);
    rhi::ShaderCursor cursor = bindingCursor(pass, set, binding);
    if (!resolved || !cursor.isValid() || offset > resolved->getDesc().size) {
        return GPU_ERROR_INVALID_ARGS;
    }
    const auto usage = resolved->getDesc().usage;
    if ((access == GPU_BUFFER_BINDING_READ &&
         (usage & rhi::BufferUsage::ShaderResource) == rhi::BufferUsage::None) ||
        (access == GPU_BUFFER_BINDING_READ_WRITE &&
         (usage & rhi::BufferUsage::UnorderedAccess) == rhi::BufferUsage::None) ||
        (access == GPU_BUFFER_BINDING_CONSTANT &&
         (usage & rhi::BufferUsage::ConstantBuffer) == rhi::BufferUsage::None)) {
        return GPU_ERROR_INVALID_ARGS;
    }
    const uint64_t resolvedRange = range ? range : resolved->getDesc().size - offset;
    if (resolvedRange > resolved->getDesc().size - offset) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(cursor.setBinding(
        rhi::Binding(resolved, rhi::BufferRange{offset, resolvedRange})))
        ? GPU_SUCCESS
        : GPU_ERROR_INTERNAL;
}

GpuResult gpuCmdSetBindingTexture(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding, GpuTextureHandle texture)
{
    if (!pass || !gpuHandleIsValid(texture)) return GPU_ERROR_INVALID_ARGS;
    rhi::ITexture* resolved = pass->device->texturePool.resolve(texture.index, texture.generation);
    rhi::ShaderCursor cursor = bindingCursor(pass, set, binding);
    if (!resolved || !cursor.isValid()) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(cursor.setBinding(rhi::Binding(resolved))) ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}

GpuResult gpuCmdSetBindingSampler(GpuRenderPassEncoder pass, uint32_t set, uint32_t binding, GpuHandle sampler)
{
    if (!pass || !gpuHandleIsValid(sampler)) return GPU_ERROR_INVALID_ARGS;
    rhi::ISampler* resolved = pass->device->samplerPool.resolve(sampler.index, sampler.generation);
    rhi::ShaderCursor cursor = bindingCursor(pass, set, binding);
    if (!resolved || !cursor.isValid()) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(cursor.setBinding(rhi::Binding(resolved))) ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
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

void gpuCmdDrawIndexed(GpuRenderPassEncoder pass, uint32_t indexCount, uint32_t instanceCount,
                       uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    if (!pass) return;
    rhi::DrawArguments args = {};
    args.vertexCount = indexCount;
    args.instanceCount = instanceCount;
    args.startIndexLocation = firstIndex;
    args.startVertexLocation = vertexOffset;
    args.startInstanceLocation = firstInstance;
    pass->rhiPassEncoder->drawIndexed(args);
}

GpuResult gpuCmdDrawIndexedIndirectPass(
    GpuRenderPassEncoder pass,
    GpuBufferHandle argumentBuffer,
    uint64_t argumentOffset,
    uint32_t maxDrawCount,
    GpuBufferHandle countBuffer,
    uint64_t countOffset)
{
    if (!pass || !gpuHandleIsValid(argumentBuffer) || maxDrawCount == 0) {
        return GPU_ERROR_INVALID_ARGS;
    }
    rhi::IBuffer* arguments =
        pass->device->bufferPool.resolve(argumentBuffer.index, argumentBuffer.generation);
    const uint64_t drawArgumentSize = 5 * sizeof(uint32_t);
    if (!arguments ||
        gpuGetBufferState(pass->device, argumentBuffer) != GPU_RESOURCE_STATE_INDIRECT_ARGUMENT ||
        argumentOffset > arguments->getDesc().size ||
        maxDrawCount > (arguments->getDesc().size - argumentOffset) / drawArgumentSize) {
        return GPU_ERROR_INVALID_ARGS;
    }
    rhi::BufferOffsetPair count = {};
    if (gpuHandleIsValid(countBuffer)) {
        rhi::IBuffer* resolvedCount =
            pass->device->bufferPool.resolve(countBuffer.index, countBuffer.generation);
        if (!resolvedCount ||
            gpuGetBufferState(pass->device, countBuffer) != GPU_RESOURCE_STATE_INDIRECT_ARGUMENT ||
            countOffset > resolvedCount->getDesc().size ||
            sizeof(uint32_t) > resolvedCount->getDesc().size - countOffset) {
            return GPU_ERROR_INVALID_ARGS;
        }
        count = rhi::BufferOffsetPair(resolvedCount, countOffset);
    }
    pass->rhiPassEncoder->drawIndexedIndirect(
        maxDrawCount,
        rhi::BufferOffsetPair(arguments, argumentOffset),
        count);
    return GPU_SUCCESS;
}

GpuComputePassEncoder gpuCmdBeginComputePass(GpuCommandEncoder encoder)
{
    if (!encoder) return nullptr;
    auto* passEncoder = encoder->rhiEncoder->beginComputePass();
    if (!passEncoder) return nullptr;
    GpuComputePassEncoder pass = new GpuComputePassEncoder_t();
    pass->rhiPassEncoder = passEncoder;
    pass->device = encoder->device;
    return pass;
}

void gpuCmdEndComputePass(GpuComputePassEncoder pass)
{
    if (!pass) return;
    pass->rhiPassEncoder->end();
    delete pass;
}

void gpuCmdBindComputePipeline(GpuComputePassEncoder pass, GpuComputePipeline pipeline)
{
    if (!pass || !pipeline) return;
    auto* rhiPipe = static_cast<GpuComputePipeline_t*>(pipeline)->rhiPipeline.get();
    pass->rootShaderObject = pass->rhiPassEncoder->bindPipeline(rhiPipe);
}

extern rhi::IComputePipeline* gpuResolveComputePipeline(GpuDevice device, GpuPipelineHandle pipeline);

GpuResult gpuCmdBindComputePipelineHandle(GpuComputePassEncoder pass, GpuPipelineHandle pipeline)
{
    if (!pass || !gpuHandleIsValid(pipeline)) return GPU_ERROR_INVALID_ARGS;
    rhi::IComputePipeline* resolved = gpuResolveComputePipeline(pass->device, pipeline);
    if (!resolved) return GPU_ERROR_INVALID_ARGS;
    pass->rootShaderObject = pass->rhiPassEncoder->bindPipeline(resolved);
    return pass->rootShaderObject ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}

static rhi::ShaderCursor computeBindingCursor(
    GpuComputePassEncoder pass,
    uint32_t set,
    uint32_t binding)
{
    if (!pass || !pass->rootShaderObject) return {};
    rhi::ShaderCursor cursor(pass->rootShaderObject);
    slang::TypeLayoutReflection* layout = cursor.getTypeLayout();
    if (!layout || layout->getKind() != slang::TypeReflection::Kind::Struct) return {};
    const SlangInt count = layout->getFieldCount();
    for (SlangInt index = 0; index < count; ++index) {
        slang::VariableLayoutReflection* field = layout->getFieldByIndex((unsigned int)index);
        if (field && field->getBindingSpace() == set && field->getBindingIndex() == binding) {
            return cursor[(uint32_t)index];
        }
    }
    return {};
}

GpuResult gpuCmdSetComputeBindingData(
    GpuComputePassEncoder pass,
    uint32_t set,
    uint32_t binding,
    const void* data,
    size_t size)
{
    if (!data || size == 0) return GPU_ERROR_INVALID_ARGS;
    rhi::ShaderCursor cursor = computeBindingCursor(pass, set, binding);
    if (!cursor.isValid()) return GPU_ERROR_INVALID_ARGS;
    slang::TypeLayoutReflection* type = cursor.getTypeLayout();
    if (type && (type->getKind() == slang::TypeReflection::Kind::ConstantBuffer ||
                 type->getKind() == slang::TypeReflection::Kind::ParameterBlock)) {
        cursor = cursor.getDereferenced();
        if (!cursor.isValid()) return GPU_ERROR_INTERNAL;
    }
    return SLANG_SUCCEEDED(cursor.setData(data, size))
        ? GPU_SUCCESS
        : GPU_ERROR_INTERNAL;
}

GpuResult gpuCmdSetComputeBindingBuffer(
    GpuComputePassEncoder pass,
    uint32_t set,
    uint32_t binding,
    GpuBufferHandle buffer,
    uint64_t offset,
    uint64_t range,
    GpuBufferBindingAccess access)
{
    if (!pass || !gpuHandleIsValid(buffer) || access > GPU_BUFFER_BINDING_CONSTANT) {
        return GPU_ERROR_INVALID_ARGS;
    }
    rhi::IBuffer* resolved = pass->device->bufferPool.resolve(buffer.index, buffer.generation);
    rhi::ShaderCursor cursor = computeBindingCursor(pass, set, binding);
    if (!resolved || !cursor.isValid() || offset > resolved->getDesc().size) {
        return GPU_ERROR_INVALID_ARGS;
    }
    const auto usage = resolved->getDesc().usage;
    if ((access == GPU_BUFFER_BINDING_READ &&
         (usage & rhi::BufferUsage::ShaderResource) == rhi::BufferUsage::None) ||
        (access == GPU_BUFFER_BINDING_READ_WRITE &&
         (usage & rhi::BufferUsage::UnorderedAccess) == rhi::BufferUsage::None) ||
        (access == GPU_BUFFER_BINDING_CONSTANT &&
         (usage & rhi::BufferUsage::ConstantBuffer) == rhi::BufferUsage::None)) {
        return GPU_ERROR_INVALID_ARGS;
    }
    const uint64_t resolvedRange = range ? range : resolved->getDesc().size - offset;
    if (resolvedRange > resolved->getDesc().size - offset) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(cursor.setBinding(
        rhi::Binding(resolved, rhi::BufferRange{offset, resolvedRange})))
        ? GPU_SUCCESS
        : GPU_ERROR_INTERNAL;
}

GpuResult gpuCmdSetComputeBindingTexture(
    GpuComputePassEncoder pass,
    uint32_t set,
    uint32_t binding,
    GpuTextureHandle texture)
{
    if (!pass || !gpuHandleIsValid(texture)) return GPU_ERROR_INVALID_ARGS;
    rhi::ITexture* resolved = pass->device->texturePool.resolve(texture.index, texture.generation);
    rhi::ShaderCursor cursor = computeBindingCursor(pass, set, binding);
    if (!resolved || !cursor.isValid()) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(cursor.setBinding(rhi::Binding(resolved)))
        ? GPU_SUCCESS
        : GPU_ERROR_INTERNAL;
}

GpuResult gpuCmdSetComputeBindingSampler(
    GpuComputePassEncoder pass,
    uint32_t set,
    uint32_t binding,
    GpuHandle sampler)
{
    if (!pass || !gpuHandleIsValid(sampler)) return GPU_ERROR_INVALID_ARGS;
    rhi::ISampler* resolved = pass->device->samplerPool.resolve(sampler.index, sampler.generation);
    rhi::ShaderCursor cursor = computeBindingCursor(pass, set, binding);
    if (!resolved || !cursor.isValid()) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(cursor.setBinding(rhi::Binding(resolved)))
        ? GPU_SUCCESS
        : GPU_ERROR_INTERNAL;
}

void gpuCmdDispatchCompute(GpuComputePassEncoder pass, uint32_t x, uint32_t y, uint32_t z)
{
    if (!pass) return;
    pass->rhiPassEncoder->dispatchCompute(x, y, z);
}

GpuResult gpuCmdDispatchComputeIndirect(
    GpuComputePassEncoder pass,
    GpuBufferHandle argumentBuffer,
    uint64_t argumentOffset)
{
    if (!pass || !gpuHandleIsValid(argumentBuffer)) return GPU_ERROR_INVALID_ARGS;
    rhi::IBuffer* resolved =
        pass->device->bufferPool.resolve(argumentBuffer.index, argumentBuffer.generation);
    if (!resolved ||
        gpuGetBufferState(pass->device, argumentBuffer) != GPU_RESOURCE_STATE_INDIRECT_ARGUMENT ||
        argumentOffset > resolved->getDesc().size ||
        3 * sizeof(uint32_t) > resolved->getDesc().size - argumentOffset) {
        return GPU_ERROR_INVALID_ARGS;
    }
    pass->rhiPassEncoder->dispatchComputeIndirect(
        rhi::BufferOffsetPair(resolved, argumentOffset));
    return GPU_SUCCESS;
}
