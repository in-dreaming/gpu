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
        .title = "00_window",
        .width = 800,
        .height = 600,
        .resizable = true,
        .vsync = true,
    };
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) {
        printf("Failed to create window: %d\n", res);
        gpuPlatformShutdown();
        return 1;
    }

    GpuDevice device;
    GpuDeviceDesc devDesc = {
        .appName = "00_window",
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

    printf("Window %ux%u created with surface\n",
           gpuWindowGetWidth(window), gpuWindowGetHeight(window));

    GpuPlatformEvent ev;
    for (;;) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) goto cleanup;
            if (ev.type == GPU_PLATFORM_EVENT_RESIZE)
                printf("Resized: %ux%u\n", ev.resize.width, ev.resize.height);
        }
    }

cleanup:
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();
    return 0;
}
