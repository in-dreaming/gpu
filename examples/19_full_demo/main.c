#include "gpu/gpu.h"
#include "gpu/capability/gpu_feature_gating.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/ai/gpu_neural.h"
#include "gpu/raytracing/gpu_raytracing.h"
#include "gpu/workgraph/gpu_work_graph.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    printf("=== Full Neural Rendering Demo (Phase 3) ===\n\n");
    printf("Note: slang-rhi handles backend selection internally\n\n");

    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuWindow window = NULL;
    GpuWindowDesc winDesc = { .title = "19_full_demo", .width = 800, .height = 600 };
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) { printf("Window failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "19_full_demo", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue graphicsQueue = NULL;
    GpuCommandQueue computeQueue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &graphicsQueue);
    if (res != GPU_SUCCESS) { printf("Graphics queue failed: %d\n", res); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }
    gpuGetQueue(device, GPU_QUEUE_TYPE_COMPUTE, &computeQueue);

    GpuSurface surface = NULL;
    res = gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    if (res != GPU_SUCCESS) { printf("Surface failed: %d\n", res); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuFormat fmt = gpuSurfaceGetPreferredFormat(surface);
    gpuSurfaceConfigure(surface, 800, 600, fmt, true);

    printf("\n2. Building Feature Table...\n");

    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);

    printf("   Bindless:           %s\n", features.supported[GPU_FEATURE_BINDLESS] ? "YES" : "NO");
    printf("   Ray Tracing:        %s\n", features.supported[GPU_FEATURE_RAY_TRACING] ? "YES" : "NO");
    printf("   Work Graph:         %s\n", features.supported[GPU_FEATURE_WORK_GRAPH] ? "YES" : "NO");
    printf("   Cooperative Matrix: %s\n", features.supported[GPU_FEATURE_COOPERATIVE_MATRIX] ? "YES" : "NO");

    printf("\n3. Creating Ray Tracing resources...\n");

    if (features.supported[GPU_FEATURE_RAY_TRACING]) {
        printf("   Ray Tracing supported (would build AS here)\n");
    } else {
        printf("   Ray Tracing not supported, skipping\n");
    }

    printf("\n4. Creating Neural Network...\n");

    GpuNeuralNetworkDesc nnDesc = {
        .inputWidth = 256,
        .inputHeight = 256,
        .hiddenDim = 64,
        .outputChannels = 3,
        .weightFormat = GPU_TENSOR_FORMAT_F16,
    };

    GpuNeuralNetwork neuralNet = NULL;
    res = gpuCreateNeuralNetwork(device, &nnDesc, &neuralNet);
    printf("   Neural Network: %s\n",
           res == GPU_SUCCESS ? "created" : "not supported (stub)");

    printf("\n5. Configuring Work Graph...\n");

    GpuWorkGraph wg = NULL;
    if (features.supported[GPU_FEATURE_WORK_GRAPH]) {
        printf("   Work Graph supported (would create graph here)\n");
    } else {
        printf("   Work Graph not supported, skipping\n");
    }

    printf("\n6. Rendering 30 frames...\n");

    uint32_t frameCount = 0;
    GpuPlatformEvent ev;
    int quit = 0;

    while (frameCount < 30 && !quit) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) { quit = 1; break; }
        }
        if (quit) break;

        GpuSurfaceTexture backbuffer = NULL;
        res = gpuSurfaceAcquireNextImage(surface, &backbuffer);
        if (res != GPU_SUCCESS) continue;

        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, graphicsQueue);
        if (!encoder) { gpuSurfaceTextureRelease(backbuffer); continue; }

        GpuRenderPassColorAttachment colorAttachment = {
            .attachment = backbuffer,
            .textureHandle = {0, 0},
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearValue = { 0.05f, 0.05f, 0.1f, 1.0f },
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
            gpuQueueSubmit(graphicsQueue, 1, &cmd);
        }

        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(backbuffer);
        frameCount++;
    }

    printf("   Rendered %u frames\n", frameCount);

    printf("\n=== Full neural rendering demo completed ===\n");

    gpuDestroyWorkGraph(device, wg);
    gpuDestroyNeuralNetwork(device, neuralNet);
    gpuSurfaceUnconfigure(surface);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();

    return 0;
}
