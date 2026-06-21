#include "gpu/ai/gpu_tensor.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_internal.h"
#include <string.h>
#include <stdlib.h>

static bool tensorDescIsValid(const GpuTensorDesc* desc)
{
    if (!desc || desc->dimCount == 0 || desc->dimCount > 4) return false;
    for (uint32_t i = 0; i < desc->dimCount; i++) {
        if (desc->dims[i] == 0) return false;
    }
    return true;
}

static void fillDefaultStrides(GpuTensorDesc* desc)
{
    uint32_t stride = 1;
    for (int32_t i = (int32_t)desc->dimCount - 1; i >= 0; i--) {
        if (desc->strides[i] == 0) {
            desc->strides[i] = stride;
        }
        stride *= desc->dims[i];
    }
    for (uint32_t i = desc->dimCount; i < 4; i++) {
        desc->dims[i] = 1;
        desc->strides[i] = 0;
    }
}

static size_t calculateTensorStorageSize(const GpuTensorDesc* desc)
{
    if (!tensorDescIsValid(desc)) return 0;

    GpuTensorDesc normalized = *desc;
    fillDefaultStrides(&normalized);

    size_t maxElementOffset = 0;
    for (uint32_t i = 0; i < normalized.dimCount; i++) {
        maxElementOffset += (size_t)(normalized.dims[i] - 1) * normalized.strides[i];
    }
    return (maxElementOffset + 1) * gpuGetTensorElementSize(normalized.format);
}

static void destroyTensorStorage(GpuDevice device, GpuTensorStorage* storage)
{
    if (!device || !storage) return;
    if (storage->refCount > 0) storage->refCount--;
    if (storage->refCount == 0) {
        gpuDestroyBuffer(device, storage->bufferHandle);
        free(storage);
    }
}

uint32_t gpuGetTensorElementSize(GpuTensorFormat format)
{
    switch (format) {
    case GPU_TENSOR_FORMAT_F32: return 4;
    case GPU_TENSOR_FORMAT_F16: return 2;
    case GPU_TENSOR_FORMAT_BF16: return 2;
    case GPU_TENSOR_FORMAT_I8:  return 1;
    case GPU_TENSOR_FORMAT_U8:  return 1;
    default: return 4;
    }
}

const char* gpuTensorFormatToString(GpuTensorFormat format)
{
    switch (format) {
    case GPU_TENSOR_FORMAT_F32: return "f32";
    case GPU_TENSOR_FORMAT_F16: return "f16";
    case GPU_TENSOR_FORMAT_BF16: return "bf16";
    case GPU_TENSOR_FORMAT_I8:  return "i8";
    case GPU_TENSOR_FORMAT_U8:  return "u8";
    default: return "unknown";
    }
}

size_t gpuCalculateTensorSize(const GpuTensorDesc* desc)
{
    if (!tensorDescIsValid(desc)) return 0;

    size_t totalElements = 1;
    for (uint32_t i = 0; i < desc->dimCount; i++) {
        totalElements *= desc->dims[i];
    }

    return totalElements * gpuGetTensorElementSize(desc->format);
}

GpuResult gpuCreateTensor(GpuDevice device, const GpuTensorDesc* desc, GpuTensorHandle* outHandle)
{
    if (!device || !desc || !outHandle) return GPU_ERROR_INVALID_ARGS;
    if (!tensorDescIsValid(desc)) return GPU_ERROR_INVALID_ARGS;

    GpuTensorDesc normalized = *desc;
    fillDefaultStrides(&normalized);

    size_t size = calculateTensorStorageSize(&normalized);
    if (size == 0) return GPU_ERROR_INVALID_ARGS;

    // Create underlying buffer for tensor data
    GpuBufferDesc bufferDesc = {};
    bufferDesc.size = size;
    bufferDesc.usage = (GpuBufferUsage)(GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_UNORDERED_ACCESS);

    GpuBufferHandle bufferHandle;
    GpuResult res = gpuCreateBuffer(device, &bufferDesc, &bufferHandle);
    if (res != GPU_SUCCESS) {
        return res;
    }

    // Allocate tensor metadata on heap
    GpuTensorStorage* storage = (GpuTensorStorage*)malloc(sizeof(GpuTensorStorage));
    if (!storage) {
        gpuDestroyBuffer(device, bufferHandle);
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    storage->bufferHandle = bufferHandle;
    storage->bufferSize = size;
    storage->refCount = 1;

    GpuTensorData* tensorData = (GpuTensorData*)malloc(sizeof(GpuTensorData));
    if (!tensorData) {
        destroyTensorStorage(device, storage);
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    tensorData->format = (uint32_t)normalized.format;
    tensorData->dimCount = normalized.dimCount;
    tensorData->logicalSize = gpuCalculateTensorSize(&normalized);
    tensorData->storage = storage;
    
    for (uint32_t i = 0; i < 4; i++) {
        tensorData->dims[i] = normalized.dims[i];
        tensorData->strides[i] = normalized.strides[i];
    }

    uint32_t index = device->tensorPool.allocate(tensorData);
    if (index == 0) {
        free(tensorData);
        destroyTensorStorage(device, storage);
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    // Get generation from the slot
    outHandle->index = index;
    outHandle->generation = device->tensorPool.slots[index].generation;

    return GPU_SUCCESS;
}

GpuResult gpuDestroyTensor(GpuDevice device, GpuTensorHandle tensor)
{
    if (!device) return GPU_ERROR_INVALID_ARGS;
    if (!tensor.index) return GPU_SUCCESS;  // Null handle

    GpuTensorData* data = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!data) return GPU_ERROR_INVALID_ARGS;

    destroyTensorStorage(device, data->storage);
    free(data);

    // Free tensor slot
    device->tensorPool.release(tensor.index, tensor.generation);

    return GPU_SUCCESS;
}

GpuBufferHandle gpuGetTensorBuffer(GpuDevice device, GpuTensorHandle tensor)
{
    if (!device || !tensor.index) return GPU_NULL_HANDLE;

    GpuTensorData* data = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!data) return GPU_NULL_HANDLE;

    return data->storage ? data->storage->bufferHandle : GPU_NULL_HANDLE;
}

uint64_t gpuGetTensorBufferSize(GpuDevice device, GpuTensorHandle tensor)
{
    if (!device || !tensor.index) return 0;

    GpuTensorData* data = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!data) return 0;

    return data->storage ? (uint64_t)data->storage->bufferSize : 0;
}

const GpuTensorData* gpuGetTensorData(GpuDevice device, GpuTensorHandle tensor)
{
    if (!device || !tensor.index) return nullptr;
    return device->tensorPool.resolve(tensor.index, tensor.generation);
}

// ============================================================================
// Tensor Data Upload/Download
// ============================================================================

GpuResult gpuUploadTensor(GpuDevice device, GpuTensorHandle tensor, const void* data, size_t size)
{
    if (!device || !tensor.index || !data || size == 0) return GPU_ERROR_INVALID_ARGS;

    GpuTensorData* tensorData = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!tensorData) return GPU_ERROR_INVALID_ARGS;

    // Validate size doesn't exceed tensor size
    if (!tensorData->storage || size > tensorData->logicalSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Use buffer upload function
    return gpuUploadToBuffer(device, tensorData->storage->bufferHandle, data, size, 0);
}

GpuResult gpuDownloadTensor(GpuDevice device, GpuTensorHandle tensor, void* outData, size_t size)
{
    if (!device || !tensor.index || !outData || size == 0) return GPU_ERROR_INVALID_ARGS;

    GpuTensorData* tensorData = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!tensorData) return GPU_ERROR_INVALID_ARGS;

    // Validate size doesn't exceed tensor size
    if (!tensorData->storage || size > tensorData->logicalSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Use buffer download function
    return gpuDownloadFromBuffer(device, tensorData->storage->bufferHandle, outData, size, 0);
}

// ============================================================================
// Tensor Copy (GPU -> GPU)
// ============================================================================

GpuResult gpuCmdCopyTensor(GpuCommandBuffer cmd, GpuTensorHandle dst, GpuTensorHandle src)
{
    if (!cmd || !cmd->device || !dst.index || !src.index) return GPU_ERROR_INVALID_ARGS;

    GpuDevice device = cmd->device;

    GpuTensorData* srcData = device->tensorPool.resolve(src.index, src.generation);
    GpuTensorData* dstData = device->tensorPool.resolve(dst.index, dst.generation);

    if (!srcData || !dstData) return GPU_ERROR_INVALID_ARGS;

    if (!srcData->storage || !dstData->storage || dstData->logicalSize < srcData->logicalSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    rhi::IBuffer* srcBuf = device->bufferPool.resolve(srcData->storage->bufferHandle.index, srcData->storage->bufferHandle.generation);
    rhi::IBuffer* dstBuf = device->bufferPool.resolve(dstData->storage->bufferHandle.index, dstData->storage->bufferHandle.generation);

    if (!srcBuf || !dstBuf) return GPU_ERROR_INVALID_ARGS;

    if (cmd->rhiEncoder) {
        cmd->rhiEncoder->copyBuffer(dstBuf, 0, srcBuf, 0, srcData->logicalSize);
    }

    return GPU_SUCCESS;
}

// ============================================================================
// Tensor Fill
// ============================================================================

GpuResult gpuCmdFillTensor(GpuCommandBuffer cmd, GpuTensorHandle tensor, float /*value*/)
{
    if (!cmd || !cmd->device || !tensor.index) return GPU_ERROR_INVALID_ARGS;

    GpuDevice device = cmd->device;

    GpuTensorData* tensorData = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!tensorData) return GPU_ERROR_INVALID_ARGS;

    if (!tensorData->storage) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* buf = device->bufferPool.resolve(tensorData->storage->bufferHandle.index, tensorData->storage->bufferHandle.generation);
    if (!buf) return GPU_ERROR_INVALID_ARGS;

    if (cmd->rhiEncoder) {
        cmd->rhiEncoder->clearBuffer(buf, 0, tensorData->storage->bufferSize);
    }

    return GPU_SUCCESS;
}

// ============================================================================
// Tensor View Creation
// ============================================================================

GpuResult gpuCreateTensorView(GpuDevice device, GpuTensorHandle src, const GpuTensorDesc* viewDesc, GpuTensorHandle* outView)
{
    if (!device || !src.index || !viewDesc || !outView) return GPU_ERROR_INVALID_ARGS;
    if (!tensorDescIsValid(viewDesc)) return GPU_ERROR_INVALID_ARGS;

    // Resolve source tensor
    GpuTensorData* srcData = device->tensorPool.resolve(src.index, src.generation);
    if (!srcData) return GPU_ERROR_INVALID_ARGS;

    if (!srcData->storage) return GPU_ERROR_INVALID_ARGS;

    GpuTensorDesc normalized = *viewDesc;
    fillDefaultStrides(&normalized);

    size_t logicalSize = gpuCalculateTensorSize(&normalized);
    size_t requiredSize = calculateTensorStorageSize(&normalized);
    if (logicalSize == 0 || requiredSize == 0) return GPU_ERROR_INVALID_ARGS;

    // Validate view fits within source buffer
    if (requiredSize > srcData->storage->bufferSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Allocate tensor metadata for view (shares buffer with source)
    GpuTensorData* viewData = (GpuTensorData*)malloc(sizeof(GpuTensorData));
    if (!viewData) return GPU_ERROR_OUT_OF_MEMORY;

    viewData->format = (uint32_t)normalized.format;
    viewData->dimCount = normalized.dimCount;
    viewData->logicalSize = logicalSize;
    viewData->storage = srcData->storage;
    viewData->storage->refCount++;

    // Copy dimensions and strides
    for (uint32_t i = 0; i < 4; i++) {
        viewData->dims[i] = normalized.dims[i];
        viewData->strides[i] = normalized.strides[i];
    }

    uint32_t index = device->tensorPool.allocate(viewData);
    if (index == 0) {
        viewData->storage->refCount--;
        free(viewData);
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    // Get generation from the slot
    outView->index = index;
    outView->generation = device->tensorPool.slots[index].generation;

    return GPU_SUCCESS;
}

// ============================================================================
// Tensor Information Queries
// ============================================================================

GpuResult gpuGetTensorDesc(GpuDevice device, GpuTensorHandle tensor, GpuTensorDesc* outDesc)
{
    if (!device || !tensor.index || !outDesc) return GPU_ERROR_INVALID_ARGS;

    GpuTensorData* data = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!data) return GPU_ERROR_INVALID_ARGS;

    outDesc->format = (GpuTensorFormat)data->format;
    outDesc->dimCount = data->dimCount;

    for (uint32_t i = 0; i < 4; i++) {
        outDesc->dims[i] = data->dims[i];
        outDesc->strides[i] = data->strides[i];
    }

    return GPU_SUCCESS;
}

bool gpuTensorIsContiguous(GpuDevice device, GpuTensorHandle tensor)
{
    if (!device || !tensor.index) return false;

    GpuTensorData* data = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!data) return false;

    // Check if strides match contiguous layout
    // For contiguous memory: stride[i] = product of dims[i+1:]
    uint32_t expectedStride = 1;
    for (int32_t i = (int32_t)data->dimCount - 1; i >= 0; i--) {
        if (data->strides[i] != expectedStride) {
            return false;
        }
        expectedStride *= data->dims[i];
    }

    return true;
}
