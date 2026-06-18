#include "gpu/gpu.h"
#include "gpu/capability/gpu_feature_gating.h"
#include "gpu/capability/gpu_fallback_registry.h"
#include <stdio.h>

int main(void)
{
    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuWindow window = NULL;
    GpuWindowDesc winDesc = { .title = "14_feature_gating", .width = 800, .height = 600 };
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) { printf("Window failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "14_feature_gating", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuFeatureTable features;
    res = gpuBuildFeatureTable(device, &features);
    if (res != GPU_SUCCESS) { printf("Feature table build failed: %d\n", res); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    printf("Feature Matrix:\n");
    printf("  Bindless:           %s (limit=%u)\n",
           features.supported[GPU_FEATURE_BINDLESS] ? "YES" : "NO",
           features.limits[GPU_FEATURE_BINDLESS]);
    printf("  Mesh Shader:        %s\n",
           features.supported[GPU_FEATURE_MESH_SHADER] ? "YES" : "NO");
    printf("  Ray Tracing:        %s\n",
           features.supported[GPU_FEATURE_RAY_TRACING] ? "YES" : "NO");
    printf("  Work Graph:         %s\n",
           features.supported[GPU_FEATURE_WORK_GRAPH] ? "YES" : "NO");
    printf("  Cooperative Matrix: %s\n",
           features.supported[GPU_FEATURE_COOPERATIVE_MATRIX] ? "YES" : "NO");
    printf("  Sparse Resource:    %s\n",
           features.supported[GPU_FEATURE_SPARSE_RESOURCE] ? "YES" : "NO");
    printf("  Async Compute:      %s\n",
           features.supported[GPU_FEATURE_ASYNC_COMPUTE] ? "YES" : "NO");

    if (!features.supported[GPU_FEATURE_MESH_SHADER]) {
        printf("\nMesh Shader not supported, fallback would be used.\n");
    }

    char jsonBuffer[1024];
    res = gpuFeatureTableToJson(&features, jsonBuffer, sizeof(jsonBuffer));
    if (res == GPU_SUCCESS) {
        printf("\nFeature Table JSON:\n%s\n", jsonBuffer);

        GpuFeatureTable restored;
        res = gpuFeatureTableFromJson(jsonBuffer, &restored);
        if (res == GPU_SUCCESS) {
            printf("JSON round-trip verification: ");
            bool match = true;
            for (int i = 0; i < GPU_FEATURE_COUNT; i++) {
                if (features.supported[i] != restored.supported[i] ||
                    features.limits[i] != restored.limits[i]) {
                    match = false;
                    break;
                }
            }
            printf("%s\n", match ? "PASSED" : "FAILED");
        }
    }

    printf("\nFeature gating test completed successfully.\n");

    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();

    return 0;
}
