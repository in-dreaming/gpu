#include "gpu/gpu.h"
#include <stdio.h>

int main(void)
{
    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) {
        printf("Failed to init platform: %d\n", res);
        return 1;
    }

    GpuWindow window;
    GpuWindowDesc winDesc = {
        .title = "00_window_clear",
        .width = 800,
        .height = 600,
        .vsync = true,
        .resizable = true,
    };
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) {
        printf("Failed to create window: %d\n", res);
        gpuPlatformShutdown();
        return 1;
    }

    GpuDevice device;
    GpuDeviceDesc devDesc = {
        .appName = "00_window_clear",
        .enableDebugLayer = false,
    };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) {
        printf("Failed to create device: %d\n", res);
        gpuDestroyWindow(window);
        gpuPlatformShutdown();
        return 1;
    }

    GpuSurface surface;
    res = gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    if (res != GPU_SUCCESS) {
        printf("Failed to create surface: %d\n", res);
        gpuDestroyDevice(device);
        gpuDestroyWindow(window);
        gpuPlatformShutdown();
        return 1;
    }

    GpuFormat fmt = gpuSurfaceGetPreferredFormat(surface);
    res = gpuSurfaceConfigure(surface, 800, 600, fmt, true);
    if (res != GPU_SUCCESS) {
        printf("Failed to configure surface: %d\n", res);
        gpuDestroySurface(device, surface);
        gpuDestroyDevice(device);
        gpuDestroyWindow(window);
        gpuPlatformShutdown();
        return 1;
    }

    GpuCommandQueue queue;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) {
        printf("Failed to get queue: %d\n", res);
        gpuSurfaceUnconfigure(surface);
        gpuDestroySurface(device, surface);
        gpuDestroyDevice(device);
        gpuDestroyWindow(window);
        gpuPlatformShutdown();
        return 1;
    }

    uint32_t frameCount = 0;
    GpuPlatformEvent ev;
    while (frameCount < 300) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) goto done;
            if (ev.type == GPU_PLATFORM_EVENT_RESIZE)
                gpuSurfaceConfigure(surface, ev.resize.width, ev.resize.height, fmt, true);
        }

        GpuSurfaceTexture backbuffer = NULL;
        res = gpuSurfaceAcquireNextImage(surface, &backbuffer);
        if (res != GPU_SUCCESS) continue;

        GpuCommandEncoder encoder = gpuBeginCommandEncoder(queue);
        if (!encoder) {
            gpuSurfaceTextureRelease(backbuffer);
            continue;
        }

        gpuCmdClearSurfaceTexture(encoder, backbuffer, 0.1f, 0.1f, 0.2f, 1.0f);

        GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
        if (cmd) {
            gpuQueueSubmit(queue, 1, &cmd);
        }

        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(backbuffer);
        frameCount++;
    }

done:
    printf("Rendered %u frames\n", frameCount);
    gpuQueueWaitOnHost(queue);
    gpuSurfaceUnconfigure(surface);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();
    return 0;
}
