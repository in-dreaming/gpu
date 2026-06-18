#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_device.h"

// Forward declaration for command buffer operations
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPU_TENSOR_FORMAT_F32 = 0,
    GPU_TENSOR_FORMAT_F16 = 1,
    GPU_TENSOR_FORMAT_BF16 = 2,
    GPU_TENSOR_FORMAT_I8  = 3,
    GPU_TENSOR_FORMAT_U8  = 4,
} GpuTensorFormat;

typedef struct {
    GpuTensorFormat format;
    uint32_t dimCount;
    uint32_t dims[4];
    uint32_t strides[4];
} GpuTensorDesc;

typedef GpuHandle GpuTensorHandle;

// Create/destroy tensors (implemented as buffers with metadata)
GpuResult gpuCreateTensor(GpuDevice device, const GpuTensorDesc* desc, GpuTensorHandle* outHandle);
GpuResult gpuDestroyTensor(GpuDevice device, GpuTensorHandle tensor);

// Access underlying buffer
GpuBufferHandle gpuGetTensorBuffer(GpuDevice device, GpuTensorHandle tensor);
uint64_t gpuGetTensorBufferSize(GpuDevice device, GpuTensorHandle tensor);

// Utility functions
uint32_t gpuGetTensorElementSize(GpuTensorFormat format);
const char* gpuTensorFormatToString(GpuTensorFormat format);
size_t gpuCalculateTensorSize(const GpuTensorDesc* desc);

// Tensor data upload/download (CPU <-> GPU)
GpuResult gpuUploadTensor(GpuDevice device, GpuTensorHandle tensor, const void* data, size_t size);
GpuResult gpuDownloadTensor(GpuDevice device, GpuTensorHandle tensor, void* outData, size_t size);

// Tensor copy (GPU -> GPU)
GpuResult gpuCmdCopyTensor(GpuCommandBuffer cmd, GpuTensorHandle dst, GpuTensorHandle src);

// Tensor fill with value
GpuResult gpuCmdFillTensor(GpuCommandBuffer cmd, GpuTensorHandle tensor, float value);

// Create tensor view (shares memory with source, different shape)
// src: source tensor
// viewDesc: new shape/strides for the view
// outView: output view handle (shares underlying buffer with src)
GpuResult gpuCreateTensorView(GpuDevice device, GpuTensorHandle src, const GpuTensorDesc* viewDesc, GpuTensorHandle* outView);

// Tensor information queries
GpuResult gpuGetTensorDesc(GpuDevice device, GpuTensorHandle tensor, GpuTensorDesc* outDesc);
bool gpuTensorIsContiguous(GpuDevice device, GpuTensorHandle tensor);

// Internal: get tensor data (defined in cpp)
struct GpuTensorData;
const struct GpuTensorData* gpuGetTensorData(GpuDevice device, GpuTensorHandle tensor);

#ifdef __cplusplus
}
#endif
