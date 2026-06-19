#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_internal.h"

GpuResult gpuCreateBuffer(GpuDevice device, const GpuBufferDesc* desc, GpuBufferHandle* outHandle)
{
    if (!device || !desc || !outHandle) return GPU_ERROR_INVALID_ARGS;
    if (desc->size == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::BufferDesc rhiDesc = {};
    rhiDesc.size = desc->size;
    rhiDesc.elementSize = desc->elementSize;
    rhiDesc.usage = static_cast<rhi::BufferUsage>(gpuBufferUsageToRhi(desc->usage));
    rhiDesc.label = desc->label;
    rhiDesc.memoryType = rhi::MemoryType::DeviceLocal;

    rhi::ComPtr<rhi::IBuffer> rhiBuffer;
    rhi::Result rhiRes = device->rhiDevice->createBuffer(rhiDesc, nullptr, rhiBuffer.writeRef());
    if (SLANG_FAILED(rhiRes)) {
        *outHandle = GpuHandle{0, 0};
        return GPU_ERROR_INTERNAL;
    }

    uint32_t idx = device->bufferPool.allocate(rhiBuffer.detach());
    if (idx == 0) {
        *outHandle = GpuHandle{0, 0};
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outHandle->index = idx;
    outHandle->generation = device->bufferPool.slots[idx].generation;
    return GPU_SUCCESS;
}

GpuResult gpuCreateBufferInit(GpuDevice device, const GpuBufferDesc* desc, const void* initData, GpuBufferHandle* outHandle)
{
    if (!device || !desc || !initData || !outHandle) return GPU_ERROR_INVALID_ARGS;
    if (desc->size == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::BufferDesc rhiDesc = {};
    rhiDesc.size = desc->size;
    rhiDesc.elementSize = desc->elementSize;
    rhiDesc.usage = static_cast<rhi::BufferUsage>(gpuBufferUsageToRhi(desc->usage));
    rhiDesc.label = desc->label;
    rhiDesc.memoryType = rhi::MemoryType::DeviceLocal;

    rhi::ComPtr<rhi::IBuffer> rhiBuffer;
    rhi::Result rhiRes = device->rhiDevice->createBuffer(rhiDesc, initData, rhiBuffer.writeRef());
    if (SLANG_FAILED(rhiRes)) {
        *outHandle = GpuHandle{0, 0};
        return GPU_ERROR_INTERNAL;
    }

    uint32_t idx = device->bufferPool.allocate(rhiBuffer.detach());
    if (idx == 0) {
        *outHandle = GpuHandle{0, 0};
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outHandle->index = idx;
    outHandle->generation = device->bufferPool.slots[idx].generation;
    return GPU_SUCCESS;
}

GpuResult gpuDestroyBuffer(GpuDevice device, GpuBufferHandle handle)
{
    if (!device || !gpuHandleIsValid(handle)) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* buf = device->bufferPool.resolve(handle.index, handle.generation);
    if (!buf) return GPU_ERROR_INVALID_ARGS;

    buf->release();
    device->bufferPool.release(handle.index, handle.generation);
    return GPU_SUCCESS;
}

GpuResult gpuUploadToBuffer(GpuDevice device, GpuBufferHandle buffer, const void* data, size_t size, uint64_t offset)
{
    if (!device || !gpuHandleIsValid(buffer) || !data || size == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* buf = device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!buf) return GPU_ERROR_INVALID_ARGS;

    void* mappedData = nullptr;
    if (SLANG_SUCCEEDED(device->rhiDevice->mapBuffer(buf, rhi::CpuAccessMode::Write, &mappedData))) {
        memcpy(static_cast<uint8_t*>(mappedData) + offset, data, size);
        device->rhiDevice->unmapBuffer(buf);
        return GPU_SUCCESS;
    }

    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    if (SLANG_FAILED(device->graphicsQueue->createCommandEncoder(encoder.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }
    encoder->uploadBufferData(buf, offset, size, data);
    rhi::ComPtr<rhi::ICommandBuffer> cmdBuf;
    encoder->finish(cmdBuf.writeRef());
    rhi::SubmitDesc submit = {};
    rhi::ICommandBuffer* cmd = cmdBuf.get();
    submit.commandBuffers = &cmd;
    submit.commandBufferCount = 1;
    device->graphicsQueue->submit(submit);
    device->graphicsQueue->waitOnHost();
    return GPU_SUCCESS;
}

GpuResult gpuDownloadFromBuffer(GpuDevice device, GpuBufferHandle buffer, void* outData, size_t size, uint64_t offset)
{
    if (!device || !gpuHandleIsValid(buffer) || !outData || size == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* buf = device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!buf) return GPU_ERROR_INVALID_ARGS;

    void* mappedData = nullptr;
    if (SLANG_SUCCEEDED(device->rhiDevice->mapBuffer(buf, rhi::CpuAccessMode::Read, &mappedData))) {
        memcpy(outData, static_cast<uint8_t*>(mappedData) + offset, size);
        device->rhiDevice->unmapBuffer(buf);
        return GPU_SUCCESS;
    }

    rhi::BufferDesc stagingDesc = {};
    stagingDesc.size = size;
    stagingDesc.usage = rhi::BufferUsage::CopyDestination;
    stagingDesc.memoryType = rhi::MemoryType::ReadBack;
    stagingDesc.label = "download_staging";

    rhi::ComPtr<rhi::IBuffer> staging;
    if (SLANG_FAILED(device->rhiDevice->createBuffer(stagingDesc, nullptr, staging.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    rhi::ComPtr<rhi::ICommandEncoder> encoder;
    if (SLANG_FAILED(device->graphicsQueue->createCommandEncoder(encoder.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }
    encoder->copyBuffer(staging, 0, buf, offset, size);
    rhi::ComPtr<rhi::ICommandBuffer> cmdBuf;
    encoder->finish(cmdBuf.writeRef());
    rhi::SubmitDesc submit = {};
    rhi::ICommandBuffer* cmd = cmdBuf.get();
    submit.commandBuffers = &cmd;
    submit.commandBufferCount = 1;
    device->graphicsQueue->submit(submit);
    device->graphicsQueue->waitOnHost();

    if (SLANG_SUCCEEDED(device->rhiDevice->mapBuffer(staging, rhi::CpuAccessMode::Read, &mappedData))) {
        memcpy(outData, mappedData, size);
        device->rhiDevice->unmapBuffer(staging);
        return GPU_SUCCESS;
    }
    return GPU_ERROR_INTERNAL;
}
