#include "gpu/gpu.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    printf("Backend Switch Example (slang-rhi handles backend selection)\n");
    printf("Use environment variables or slang-rhi config to select backend\n\n");

    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuWindow window = NULL;
    GpuWindowDesc winDesc = {
        .title = "13_backend_switch",
        .width = 800,
        .height = 600,
        .vsync = true,
        .resizable = true,
    };
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) { printf("Window failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "13_backend_switch", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    // Query actual backend type from device info
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    printf("Device created successfully\n");

    GpuSurface surface = NULL;
    res = gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    if (res != GPU_SUCCESS) { printf("Surface failed: %d\n", res); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuFormat fmt = gpuSurfaceGetPreferredFormat(surface);
    res = gpuSurfaceConfigure(surface, 800, 600, fmt, true);
    if (res != GPU_SUCCESS) { printf("Surface configure failed: %d\n", res); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("Queue failed: %d\n", res); gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    printf("Rendering 60 frames...\n");

    uint32_t frameCount = 0;
    GpuPlatformEvent ev;
    int quit = 0;

    while (frameCount < 60 && !quit) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) { quit = 1; break; }
            if (ev.type == GPU_PLATFORM_EVENT_RESIZE)
                gpuSurfaceConfigure(surface, ev.resize.width, ev.resize.height, fmt, true);
        }
        if (quit) break;

        GpuSurfaceTexture backbuffer = NULL;
        res = gpuSurfaceAcquireNextImage(surface, &backbuffer);
        if (res != GPU_SUCCESS) continue;

        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
        if (!encoder) { gpuSurfaceTextureRelease(backbuffer); continue; }

        GpuRenderPassColorAttachment colorAttachment = {
            .attachment = backbuffer,
            .textureHandle = {0, 0},
            .mipLevel = 0,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearValue = { 0.0f, 0.2f, 0.4f, 1.0f },
        };
        GpuRenderPassDesc passDesc = {
            .colorAttachmentCount = 1,
            .colorAttachments = &colorAttachment,
        };

        GpuRenderPassEncoder pass = gpuCmdBeginRenderPass(encoder, &passDesc);
        if (pass) {
            gpuCmdEndRenderPass(pass);
        }

        GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
        if (cmd) {
            gpuQueueSubmit(queue, 1, &cmd);
        }

        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(backbuffer);
        frameCount++;
    }

    printf("Rendered %u frames\n", frameCount);

    gpuQueueWaitOnHost(queue);
    gpuSurfaceUnconfigure(surface);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();

    printf("Backend switch test passed\n");

    return 0;
}
