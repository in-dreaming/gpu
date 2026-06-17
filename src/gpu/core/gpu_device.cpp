#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_internal.h"

GpuResult gpuCreateDevice(const GpuDeviceDesc* desc, GpuDevice* outDevice)
{
    if (!desc || !outDevice) return GPU_ERROR_INVALID_ARGS;

    rhi::DeviceDesc rhiDesc = {};
    rhiDesc.deviceType = rhi::DeviceType::Default;
    rhiDesc.enableValidation = desc->enableDebugLayer;
    rhiDesc.slang.targetProfile = "sm_6_0";

    rhi::ComPtr<rhi::IDevice> rhiDevice;
    if (SLANG_FAILED(rhi::getRHI()->createDevice(rhiDesc, rhiDevice.writeRef()))) {
        rhiDesc.deviceType = rhi::DeviceType::Vulkan;
        if (SLANG_FAILED(rhi::getRHI()->createDevice(rhiDesc, rhiDevice.writeRef()))) {
            rhiDesc.deviceType = rhi::DeviceType::D3D12;
            if (SLANG_FAILED(rhi::getRHI()->createDevice(rhiDesc, rhiDevice.writeRef()))) {
                return GPU_ERROR_DEVICE_LOST;
            }
        }
    }

    GpuDevice device = new GpuDevice_t();
    device->rhiDevice = rhiDevice;

    rhi::ComPtr<rhi::ICommandQueue> queue;
    rhiDevice->getQueue(rhi::QueueType::Graphics, queue.writeRef());
    device->graphicsQueue = queue;

    *outDevice = device;
    return GPU_SUCCESS;
}

void gpuDestroyDevice(GpuDevice device)
{
    if (!device) return;

    for (uint32_t i = 1; i < 4096; i++) {
        auto& slot = device->bufferPool.slots[i];
        if (slot.alive && slot.ptr) {
            slot.ptr->release();
            slot.ptr = nullptr;
            slot.alive = false;
        }
    }

    gpuQueueWaitOnHost((GpuCommandQueue)device->graphicsQueue.get());

    for (uint32_t i = 1; i < 4096; i++) {
        auto& slot = device->texturePool.slots[i];
        if (slot.alive && slot.ptr) {
            slot.ptr->release();
            slot.ptr = nullptr;
            slot.alive = false;
        }
    }

    device->graphicsQueue = nullptr;
    device->rhiDevice = nullptr;
    delete device;
}

GpuResult gpuGetQueue(GpuDevice device, GpuQueueType type, GpuCommandQueue* outQueue)
{
    if (!device || !outQueue) return GPU_ERROR_INVALID_ARGS;
    if (type != GPU_QUEUE_TYPE_GRAPHICS) return GPU_ERROR_NOT_SUPPORTED;
    *outQueue = (GpuCommandQueue)device->graphicsQueue.get();
    return GPU_SUCCESS;
}

GpuCommandEncoder gpuBeginCommandEncoder(GpuCommandQueue queue)
{
    if (!queue) return nullptr;

    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);
    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    if (SLANG_FAILED(rhiQueue->createCommandEncoder(encoder.writeRef()))) return nullptr;

    GpuCommandEncoder enc = new GpuCommandEncoder_t();
    enc->rhiEncoder = encoder;
    enc->queue = rhiQueue;
    return enc;
}

GpuCommandBuffer gpuFinishCommandEncoder(GpuCommandEncoder encoder)
{
    if (!encoder) return nullptr;

    rhi::ComPtr<rhi::ICommandBuffer> cmdBuffer;
    encoder->rhiEncoder->finish(cmdBuffer.writeRef());

    GpuCommandBuffer buf = new GpuCommandBuffer_t();
    buf->rhiCmdBuffer = cmdBuffer;

    delete encoder;
    return buf;
}

GpuResult gpuQueueSubmit(GpuCommandQueue queue, uint32_t count, GpuCommandBuffer* cmdBuffers)
{
    if (!queue || !cmdBuffers) return GPU_ERROR_INVALID_ARGS;

    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);

    for (uint32_t i = 0; i < count; i++) {
        if (!cmdBuffers[i]) continue;
        GpuCommandBuffer_t* buf = static_cast<GpuCommandBuffer_t*>(cmdBuffers[i]);
        rhiQueue->submit(buf->rhiCmdBuffer);
        delete buf;
    }
    return GPU_SUCCESS;
}

GpuResult gpuQueueWaitOnHost(GpuCommandQueue queue)
{
    if (!queue) return GPU_ERROR_INVALID_ARGS;
    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);
    return SLANG_SUCCEEDED(rhiQueue->waitOnHost()) ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}
