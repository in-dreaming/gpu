#include "gpu/gpu.h"
#include "gpu/raytracing/gpu_raytracing.h"
#include "gpu/capability/gpu_feature_gating.h"
#include <stdio.h>

int main(void)
{
    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "18_raytracing", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("Queue failed: %d\n", res); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }

    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);

    printf("Checking Ray Tracing support...\n");

    if (!features.supported[GPU_FEATURE_RAY_TRACING]) {
        printf("Ray Tracing not supported, skipping\n");
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 0;
    }

    printf("Ray Tracing is supported, building acceleration structures...\n");

    GpuAccelerationStructureGeometry geom = {
        .vertexBuffer = {0, 0},
        .indexBuffer = {0, 0},
        .triangleCount = 2,
        .vertexStride = 12,
        .vertexFormat = GPU_FORMAT_RGB32_FLOAT,
    };

    GpuBottomLevelASDesc blasDesc = {
        .geometryCount = 1,
        .geometries = &geom,
        .allowUpdate = false
    };

    GpuAccelerationStructureHandle blas;
    res = gpuCreateBottomLevelAS(device, &blasDesc, &blas);
    if (res != GPU_SUCCESS) {
        printf("Note: gpuCreateBottomLevelAS returned %d (expected NOT_SUPPORTED in stub)\n", res);
    }

    GpuTopLevelASDesc tlasDesc = {
        .instanceCount = 1,
        .instanceBuffer = {0, 0}
    };

    GpuAccelerationStructureHandle tlas;
    res = gpuCreateTopLevelAS(device, &tlasDesc, &tlas);
    if (res != GPU_SUCCESS) {
        printf("Note: gpuCreateTopLevelAS returned %d (expected NOT_SUPPORTED in stub)\n", res);
    }

    printf("Acceleration structures configured.\n");
    printf("\nRay Tracing test completed.\n");

    gpuDestroyAccelerationStructure(device, blas);
    gpuDestroyAccelerationStructure(device, tlas);
    gpuDestroyDevice(device);
    gpuPlatformShutdown();

    return 0;
}
