#include "gpu/platform/gpu_swapchain.h"
#include "gpu/platform/gpu_surface.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_internal.h"

struct GpuSwapchain_t {
    GpuSurface surface;
    uint32_t width;
    uint32_t height;
    GpuFormat format;
    uint32_t imageCount;
    bool vsync;
};

GpuResult gpuCreateSwapchain(GpuDevice device, const GpuSwapchainDesc* desc, GpuSwapchain* outSwapchain)
{
    if (!device || !desc || !desc->surface || !outSwapchain) return GPU_ERROR_INVALID_ARGS;

    GpuResult res = gpuSurfaceConfigure(desc->surface, desc->width, desc->height, desc->format, desc->vsync);
    if (res != GPU_SUCCESS) return res;

    GpuSwapchain swapchain = new GpuSwapchain_t();
    swapchain->surface = desc->surface;
    swapchain->width = desc->width;
    swapchain->height = desc->height;
    swapchain->format = desc->format;
    swapchain->imageCount = desc->imageCount;
    swapchain->vsync = desc->vsync;

    *outSwapchain = swapchain;
    return GPU_SUCCESS;
}

void gpuDestroySwapchain(GpuDevice device, GpuSwapchain swapchain)
{
    (void)device;
    if (!swapchain) return;
    if (swapchain->surface) {
        gpuSurfaceUnconfigure(swapchain->surface);
    }
    delete swapchain;
}

GpuResult gpuSwapchainAcquireNextImage(GpuSwapchain swapchain, GpuSurfaceTexture* outTexture)
{
    if (!swapchain) return GPU_ERROR_INVALID_ARGS;
    return gpuSurfaceAcquireNextImage(swapchain->surface, outTexture);
}

GpuResult gpuSwapchainPresent(GpuSwapchain swapchain)
{
    if (!swapchain) return GPU_ERROR_INVALID_ARGS;
    return gpuSurfacePresent(swapchain->surface);
}

uint32_t gpuSwapchainGetWidth(GpuSwapchain swapchain)
{
    return swapchain ? swapchain->width : 0;
}

uint32_t gpuSwapchainGetHeight(GpuSwapchain swapchain)
{
    return swapchain ? swapchain->height : 0;
}

uint32_t gpuSwapchainGetImageCount(GpuSwapchain swapchain)
{
    return swapchain ? swapchain->imageCount : 0;
}

GpuResult gpuSwapchainResize(GpuSwapchain swapchain, uint32_t width, uint32_t height)
{
    if (!swapchain) return GPU_ERROR_INVALID_ARGS;

    GpuResult res = gpuSurfaceUnconfigure(swapchain->surface);
    if (res != GPU_SUCCESS) return res;

    res = gpuSurfaceConfigure(swapchain->surface, width, height, swapchain->format, swapchain->vsync);
    if (res != GPU_SUCCESS) return res;

    swapchain->width = width;
    swapchain->height = height;
    return GPU_SUCCESS;
}
