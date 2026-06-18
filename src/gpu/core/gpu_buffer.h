#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t GpuBufferUsage;
#define GPU_BUFFER_USAGE_NONE                   0u
#define GPU_BUFFER_USAGE_VERTEX_BUFFER          (1u << 0)
#define GPU_BUFFER_USAGE_INDEX_BUFFER           (1u << 1)
#define GPU_BUFFER_USAGE_CONSTANT_BUFFER        (1u << 2)
#define GPU_BUFFER_USAGE_SHADER_RESOURCE        (1u << 3)
#define GPU_BUFFER_USAGE_UNORDERED_ACCESS       (1u << 4)
#define GPU_BUFFER_USAGE_INDIRECT_ARGUMENT      (1u << 5)
#define GPU_BUFFER_USAGE_COPY_SOURCE            (1u << 6)
#define GPU_BUFFER_USAGE_COPY_DEST              (1u << 7)
#define GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE (1u << 8)
#define GPU_BUFFER_USAGE_SPARSE                 (1u << 9)

typedef struct {
    uint64_t size;
    uint32_t elementSize;
    GpuBufferUsage usage;
    const char* label;
} GpuBufferDesc;

typedef struct GpuDevice_t* GpuDevice;

GpuResult gpuCreateBuffer(GpuDevice device, const GpuBufferDesc* desc, GpuBufferHandle* outHandle);
GpuResult gpuCreateBufferInit(GpuDevice device, const GpuBufferDesc* desc, const void* initData, GpuBufferHandle* outHandle);
GpuResult gpuDestroyBuffer(GpuDevice device, GpuBufferHandle handle);

// Upload data to existing buffer (maps buffer, copies data, unmaps)
GpuResult gpuUploadToBuffer(GpuDevice device, GpuBufferHandle buffer, const void* data, size_t size, uint64_t offset);

// Download data from existing buffer (maps buffer, copies data, unmaps)
GpuResult gpuDownloadFromBuffer(GpuDevice device, GpuBufferHandle buffer, void* outData, size_t size, uint64_t offset);

#ifdef __cplusplus
}
#endif
