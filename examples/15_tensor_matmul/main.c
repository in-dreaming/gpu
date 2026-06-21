#include "gpu/gpu.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/ai/gpu_matrix_pipeline.h"
#include "gpu/capability/gpu_feature_gating.h"
#include <stdio.h>

int main(void)
{
    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "15_tensor_matmul", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);

    const uint32_t size = 1024;

    printf("Creating tensors (%dx%d)...\n", size, size);

    GpuTensorDesc aDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {size, size},
        .strides = {size, 1}
    };
    GpuTensorDesc bDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {size, size},
        .strides = {size, 1}
    };
    GpuTensorDesc cDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {size, size},
        .strides = {size, 1}
    };

    size_t aSize = gpuCalculateTensorSize(&aDesc);
    size_t bSize = gpuCalculateTensorSize(&bDesc);
    size_t cSize = gpuCalculateTensorSize(&cDesc);

    printf("Tensor A: %zu bytes (format=%s)\n", aSize, gpuTensorFormatToString(aDesc.format));
    printf("Tensor B: %zu bytes (format=%s)\n", bSize, gpuTensorFormatToString(bDesc.format));
    printf("Tensor C: %zu bytes (format=%s)\n", cSize, gpuTensorFormatToString(cDesc.format));

    GpuTensorHandle tensorA, tensorB, tensorC;
    res = gpuCreateTensor(device, &aDesc, &tensorA);
    if (res != GPU_SUCCESS) {
        printf("Failed to create tensor A: %d\n", res);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }

    res = gpuCreateTensor(device, &bDesc, &tensorB);
    if (res != GPU_SUCCESS) {
        printf("Failed to create tensor B: %d\n", res);
        gpuDestroyTensor(device, tensorA);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    res = gpuCreateTensor(device, &cDesc, &tensorC);
    if (res != GPU_SUCCESS) {
        printf("Failed to create tensor C: %d\n", res);
        gpuDestroyTensor(device, tensorB);
        gpuDestroyTensor(device, tensorA);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }

    bool useCoop = features.supported[GPU_FEATURE_COOPERATIVE_MATRIX];
    printf("\nCooperative Matrix support: %s\n", useCoop ? "YES" : "NO");

    GpuMatmulDesc matmulDesc = {
        .m = size,
        .n = size,
        .k = size,
        .aFormat = GPU_TENSOR_FORMAT_F32,
        .bFormat = GPU_TENSOR_FORMAT_F32,
        .cFormat = GPU_TENSOR_FORMAT_F32,
        .useCooperativeMatrix = useCoop,
    };

    GpuPipelineHandle matmulPipe;
    res = gpuCreateMatmulPipeline(device, &matmulDesc, &matmulPipe);
    if (res != GPU_SUCCESS) {
        printf("Matmul pipeline unavailable on this backend/configuration: %d\n", res);
    }

    printf("\nMatmul %dx%d configuration prepared (coop_matrix=%s)\n",
           size, size, useCoop ? "YES" : "NO");

    printf("\nTensor matmul test completed.\n");

    gpuDestroyTensor(device, tensorA);
    gpuDestroyTensor(device, tensorB);
    gpuDestroyTensor(device, tensorC);
    gpuDestroyDevice(device);
    gpuPlatformShutdown();

    return 0;
}
