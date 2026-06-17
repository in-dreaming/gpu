#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_format.h"
#include "gpu/platform/gpu_surface.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuSwapchain_t* GpuSwapchain;

typedef struct {
    GpuSurface surface;
    uint32_t width;
    uint32_t height;
    GpuFormat format;
    uint32_t imageCount;
    bool vsync;
} GpuSwapchainDesc;

typedef struct GpuDevice_t* GpuDevice;

GpuResult gpuCreateSwapchain(GpuDevice device, const GpuSwapchainDesc* desc, GpuSwapchain* outSwapchain);
void gpuDestroySwapchain(GpuDevice device, GpuSwapchain swapchain);

GpuResult gpuSwapchainAcquireNextImage(GpuSwapchain swapchain, GpuSurfaceTexture* outTexture);
GpuResult gpuSwapchainPresent(GpuSwapchain swapchain);

uint32_t gpuSwapchainGetWidth(GpuSwapchain swapchain);
uint32_t gpuSwapchainGetHeight(GpuSwapchain swapchain);
uint32_t gpuSwapchainGetImageCount(GpuSwapchain swapchain);

GpuResult gpuSwapchainResize(GpuSwapchain swapchain, uint32_t width, uint32_t height);

#ifdef __cplusplus
}
#endif
