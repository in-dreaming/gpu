#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_format.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuSurface_t* GpuSurface;
typedef struct GpuSurfaceTexture_t* GpuSurfaceTexture;

typedef enum {
    GPU_SURFACE_TYPE_VULKAN = 0,
    GPU_SURFACE_TYPE_D3D12  = 1,
    GPU_SURFACE_TYPE_METAL  = 2,
    GPU_SURFACE_TYPE_WEBGPU = 3,
} GpuSurfaceType;

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuWindow_t* GpuWindow;

GpuResult gpuCreateSurface(GpuDevice device, GpuWindow window, GpuSurfaceType type, GpuSurface* outSurface);
void gpuDestroySurface(GpuDevice device, GpuSurface surface);

uint32_t gpuSurfaceGetWidth(GpuSurface surface);
uint32_t gpuSurfaceGetHeight(GpuSurface surface);
GpuFormat gpuSurfaceGetPreferredFormat(GpuSurface surface);

GpuResult gpuSurfaceConfigure(GpuSurface surface, uint32_t width, uint32_t height, GpuFormat format, bool vsync);
GpuResult gpuSurfaceUnconfigure(GpuSurface surface);

GpuResult gpuSurfaceAcquireNextImage(GpuSurface surface, GpuSurfaceTexture* outTexture);
void gpuSurfaceTextureRelease(GpuSurfaceTexture texture);

GpuResult gpuSurfacePresent(GpuSurface surface);

#ifdef __cplusplus
}
#endif
