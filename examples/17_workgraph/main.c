#include "gpu/gpu.h"
#include "gpu/workgraph/gpu_work_graph.h"
#include "gpu/capability/gpu_feature_gating.h"
#include <stdio.h>

int main(void)
{
    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "17_workgraph", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("Queue failed: %d\n", res); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }

    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);

    printf("Checking Work Graph support...\n");

    if (!features.supported[GPU_FEATURE_WORK_GRAPH]) {
        printf("Work Graph not supported, skipping\n");
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 0;
    }

    printf("Work Graph is supported, creating graph...\n");

    static const uint8_t dummyShaderCode[] = { 0x00, 0x01, 0x02, 0x03 };

    GpuShaderBinary cullShader = { .data = dummyShaderCode, .size = (uint64_t)sizeof(dummyShaderCode) };
    GpuShaderBinary drawShader = { .data = dummyShaderCode, .size = (uint64_t)sizeof(dummyShaderCode) };

    GpuWorkGraphNodeDesc nodes[] = {
        { .name = "Cull", .nodeShader = cullShader, .entryPoint = "cullNode", .isBroadcastNode = true },
        { .name = "Draw", .nodeShader = drawShader, .entryPoint = "drawNode", .isBroadcastNode = false },
    };

    GpuWorkGraphEdge edges[] = { {0, 1} };

    GpuWorkGraphDesc wgDesc = {
        .nodeCount = 2,
        .nodes = nodes,
        .edgeCount = 1,
        .edges = edges
    };

    GpuWorkGraph wg;
    res = gpuCreateWorkGraph(device, &wgDesc, &wg);
    if (res != GPU_SUCCESS) {
        printf("Work Graph fallback creation failed: %d\n", res);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }

    printf("Work Graph configuration created: Cull -> Draw\n");
    printf("\nWork Graph test completed.\n");

    gpuDestroyWorkGraph(device, wg);
    gpuDestroyDevice(device);
    gpuPlatformShutdown();

    return 0;
}
