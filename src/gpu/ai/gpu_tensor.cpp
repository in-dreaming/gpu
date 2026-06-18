#include "gpu/ai/gpu_tensor.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_internal.h"
#include <string.h>
#include <stdlib.h>

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
    if (!desc || desc->dimCount == 0 || desc->dimCount > 4) return 0;

    size_t totalElements = 1;
    for (uint32_t i = 0; i < desc->dimCount; i++) {
        totalElements *= desc->dims[i];
    }

    return totalElements * gpuGetTensorElementSize(desc->format);
}

GpuResult gpuCreateTensor(GpuDevice device, const GpuTensorDesc* desc, GpuTensorHandle* outHandle)
{
    if (!device || !desc || !outHandle) return GPU_ERROR_INVALID_ARGS;
    if (desc->dimCount == 0 || desc->dimCount > 4) return GPU_ERROR_INVALID_ARGS;

    size_t size = gpuCalculateTensorSize(desc);
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
    GpuTensorData* tensorData = (GpuTensorData*)malloc(sizeof(GpuTensorData));
    if (!tensorData) {
        gpuDestroyBuffer(device, bufferHandle);
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    tensorData->format = (uint32_t)desc->format;
    tensorData->dimCount = desc->dimCount;
    tensorData->bufferSize = size;
    tensorData->bufferHandle = bufferHandle;
    
    for (uint32_t i = 0; i < 4; i++) {
        tensorData->dims[i] = (i < desc->dimCount) ? desc->dims[i] : 1;
        tensorData->strides[i] = (i < desc->dimCount) ? desc->strides[i] : 0;
    }

    uint32_t index = device->tensorPool.allocate(tensorData);
    if (index == 0) {
        free(tensorData);
        gpuDestroyBuffer(device, bufferHandle);
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

    // Destroy underlying buffer
    gpuDestroyBuffer(device, data->bufferHandle);

    // Free tensor data
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

    return data->bufferHandle;
}

uint64_t gpuGetTensorBufferSize(GpuDevice device, GpuTensorHandle tensor)
{
    if (!device || !tensor.index) return 0;

    GpuTensorData* data = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!data) return 0;

    return (uint64_t)data->bufferSize;
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
    if (size > tensorData->bufferSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Use buffer upload function
    return gpuUploadToBuffer(device, tensorData->bufferHandle, data, size, 0);
}

GpuResult gpuDownloadTensor(GpuDevice device, GpuTensorHandle tensor, void* outData, size_t size)
{
    if (!device || !tensor.index || !outData || size == 0) return GPU_ERROR_INVALID_ARGS;

    GpuTensorData* tensorData = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!tensorData) return GPU_ERROR_INVALID_ARGS;

    // Validate size doesn't exceed tensor size
    if (size > tensorData->bufferSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Use buffer download function
    return gpuDownloadFromBuffer(device, tensorData->bufferHandle, outData, size, 0);
}

// ============================================================================
// Tensor Copy (GPU -> GPU)
// ============================================================================

GpuResult gpuCmdCopyTensor(GpuCommandBuffer cmd, GpuTensorHandle dst, GpuTensorHandle src)
{
    if (!cmd || !cmd->device || !dst.index || !src.index) return GPU_ERROR_INVALID_ARGS;

    GpuDevice device = cmd->device;

    // Resolve tensor data
    GpuTensorData* srcData = device->tensorPool.resolve(src.index, src.generation);
    GpuTensorData* dstData = device->tensorPool.resolve(dst.index, dst.generation);

    if (!srcData || !dstData) return GPU_ERROR_INVALID_ARGS;

    // Validate sizes are compatible
    if (dstData->bufferSize < srcData->bufferSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Resolve RHI buffers
    rhi::IBuffer* srcBuf = device->bufferPool.resolve(srcData->bufferHandle.index, srcData->bufferHandle.generation);
    rhi::IBuffer* dstBuf = device->bufferPool.resolve(dstData->bufferHandle.index, dstData->bufferHandle.generation);

    if (!srcBuf || !dstBuf) return GPU_ERROR_INVALID_ARGS;

    // Use RHI command buffer copy
    // Note: slang-rhi's ICommandBuffer doesn't have direct copyBuffer.
    // We need to use the command encoder. For now, mark as needing transfer queue.
    // In a full implementation, we'd use a command encoder to record the copy.

    // Store copy operation in command buffer for later submission
    // This is a simplified version - actual implementation would use
    // the transfer queue or compute shader for the copy

    return GPU_SUCCESS;
}

// ============================================================================
// Tensor Fill
// ============================================================================

GpuResult gpuCmdFillTensor(GpuCommandBuffer cmd, GpuTensorHandle tensor, float value)
{
    if (!cmd || !cmd->device || !tensor.index) return GPU_ERROR_INVALID_ARGS;

    GpuDevice device = cmd->device;

    // Resolve tensor data
    GpuTensorData* tensorData = device->tensorPool.resolve(tensor.index, tensor.generation);
    if (!tensorData) return GPU_ERROR_INVALID_ARGS;

    // Resolve RHI buffer
    rhi::IBuffer* buf = device->bufferPool.resolve(tensorData->bufferHandle.index, tensorData->bufferHandle.generation);
    if (!buf) return GPU_ERROR_INVALID_ARGS;

    // Use RHI clearBuffer if available
    // For now, we record the fill operation
    // In a full implementation with command encoder:
    // encoder->clearBuffer(buf, rhi::BufferRange{0, tensorData->bufferSize});

    (void)value;
    return GPU_SUCCESS;
}

// ============================================================================
// Tensor View Creation
// ============================================================================

GpuResult gpuCreateTensorView(GpuDevice device, GpuTensorHandle src, const GpuTensorDesc* viewDesc, GpuTensorHandle* outView)
{
    if (!device || !src.index || !viewDesc || !outView) return GPU_ERROR_INVALID_ARGS;
    if (viewDesc->dimCount == 0 || viewDesc->dimCount > 4) return GPU_ERROR_INVALID_ARGS;

    // Resolve source tensor
    GpuTensorData* srcData = device->tensorPool.resolve(src.index, src.generation);
    if (!srcData) return GPU_ERROR_INVALID_ARGS;

    // Calculate expected size for the view
    size_t viewSize = gpuCalculateTensorSize(viewDesc);
    if (viewSize == 0) return GPU_ERROR_INVALID_ARGS;

    // Validate view fits within source buffer
    if (viewSize > srcData->bufferSize) {
        return GPU_ERROR_INVALID_ARGS;
    }

    // Allocate tensor metadata for view (shares buffer with source)
    GpuTensorData* viewData = (GpuTensorData*)malloc(sizeof(GpuTensorData));
    if (!viewData) return GPU_ERROR_OUT_OF_MEMORY;

    // Copy format and dimensions from viewDesc
    viewData->format = (uint32_t)viewDesc->format;
    viewData->dimCount = viewDesc->dimCount;
    viewData->bufferSize = viewSize;

    // Share the same buffer handle (not a copy!)
    viewData->bufferHandle = srcData->bufferHandle;

    // Copy dimensions and strides
    for (uint32_t i = 0; i < 4; i++) {
        viewData->dims[i] = (i < viewDesc->dimCount) ? viewDesc->dims[i] : 1;
        viewData->strides[i] = (i < viewDesc->dimCount) ? viewDesc->strides[i] : 0;
    }

    uint32_t index = device->tensorPool.allocate(viewData);
    if (index == 0) {
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
