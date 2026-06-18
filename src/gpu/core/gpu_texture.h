#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t GpuTextureUsage;
#define GPU_TEXTURE_USAGE_NONE              0u
#define GPU_TEXTURE_USAGE_SHADER_RESOURCE   (1u << 0)
#define GPU_TEXTURE_USAGE_UNORDERED_ACCESS  (1u << 1)
#define GPU_TEXTURE_USAGE_RENDER_TARGET     (1u << 2)
#define GPU_TEXTURE_USAGE_DEPTH_STENCIL     (1u << 3)
#define GPU_TEXTURE_USAGE_PRESENT           (1u << 4)
#define GPU_TEXTURE_USAGE_COPY_SOURCE       (1u << 5)
#define GPU_TEXTURE_USAGE_COPY_DEST         (1u << 6)
#define GPU_TEXTURE_USAGE_SPARSE            (1u << 7)

typedef enum {
    GPU_TEXTURE_TYPE_1D = 0,
    GPU_TEXTURE_TYPE_2D = 1,
    GPU_TEXTURE_TYPE_3D = 2,
    GPU_TEXTURE_TYPE_CUBE = 3,
} GpuTextureType;

typedef struct {
    GpuTextureType type;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t arrayLength;
    uint32_t mipCount;
    GpuFormat format;
    uint32_t sampleCount;
    GpuTextureUsage usage;
    const char* label;
} GpuTextureDesc;

typedef struct GpuDevice_t* GpuDevice;

typedef enum {
    GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET = 0,
    GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL = 1,
    GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE = 2,
    GPU_TEXTURE_VIEW_TYPE_UNORDERED_ACCESS = 3,
} GpuTextureViewType;

GpuResult gpuCreateTexture(GpuDevice device, const GpuTextureDesc* desc, GpuTextureHandle* outHandle);
GpuResult gpuDestroyTexture(GpuDevice device, GpuTextureHandle handle);

// Create a texture view for a specific usage (e.g., render target)
GpuResult gpuCreateTextureView(GpuDevice device, GpuTextureHandle texture, GpuTextureViewType type, GpuTextureHandle* outViewHandle);
GpuResult gpuDestroyTextureView(GpuDevice device, GpuTextureHandle viewHandle);

#ifdef __cplusplus
}
#endif
