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

#ifdef __cplusplus
}
#endif
