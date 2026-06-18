#include "gpu/gpu.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/core/gpu_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "tensor_data_test", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    printf("=== Tensor Data Operations Test ===\n\n");

    const uint32_t size = 128;
    const size_t elementSize = sizeof(float);
    const size_t tensorSize = size * size * elementSize;

    printf("Creating tensor (%dx%d, F32)...\n", size, size);

    GpuTensorDesc tensorDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {size, size},
        .strides = {size, 1}
    };

    GpuTensorHandle tensorA, tensorB;
    res = gpuCreateTensor(device, &tensorDesc, &tensorA);
    if (res != GPU_SUCCESS) {
        printf("Failed to create tensor A: %d\n", res);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    printf("Tensor A created: %zu bytes\n", gpuCalculateTensorSize(&tensorDesc));

    res = gpuCreateTensor(device, &tensorDesc, &tensorB);
    if (res != GPU_SUCCESS) {
        printf("Failed to create tensor B: %d\n", res);
        gpuDestroyTensor(device, tensorA);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    printf("Tensor B created\n");

    // Test 1: Upload data to tensor
    printf("\n--- Test 1: Upload Data ---\n");
    float* uploadData = (float*)malloc(tensorSize);
    if (!uploadData) {
        printf("Failed to allocate upload data\n");
        gpuDestroyTensor(device, tensorA);
        gpuDestroyTensor(device, tensorB);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }

    // Fill with test pattern
    for (uint32_t i = 0; i < size * size; i++) {
        uploadData[i] = (float)i;
    }

    res = gpuUploadTensor(device, tensorA, uploadData, tensorSize);
    if (res != GPU_SUCCESS) {
        printf("Failed to upload data: %d\n", res);
    } else {
        printf("Successfully uploaded %zu bytes to tensor A\n", tensorSize);
    }

    // Test 2: Download data from tensor
    printf("\n--- Test 2: Download Data ---\n");
    float* downloadData = (float*)malloc(tensorSize);
    if (!downloadData) {
        printf("Failed to allocate download data\n");
        free(uploadData);
        gpuDestroyTensor(device, tensorA);
        gpuDestroyTensor(device, tensorB);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }

    res = gpuDownloadTensor(device, tensorA, downloadData, tensorSize);
    if (res != GPU_SUCCESS) {
        printf("Failed to download data: %d\n", res);
    } else {
        printf("Successfully downloaded %zu bytes from tensor A\n", tensorSize);

        // Verify data integrity
        int errors = 0;
        for (uint32_t i = 0; i < size * size && i < 10; i++) {
            if (downloadData[i] != uploadData[i]) {
                errors++;
            }
        }
        if (errors == 0) {
            printf("Data verification: PASSED (first 10 elements match)\n");
        } else {
            printf("Data verification: FAILED (%d errors)\n", errors);
        }
    }

    // Test 3: Tensor is contiguous check
    printf("\n--- Test 3: Tensor Layout ---\n");
    bool isContig = gpuTensorIsContiguous(device, tensorA);
    printf("Tensor A is contiguous: %s\n", isContig ? "YES" : "NO");

    // Test 4: Tensor view creation
    printf("\n--- Test 4: Tensor View ---\n");
    GpuTensorDesc viewDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 1,
        .dims = {size * size},
        .strides = {1}
    };

    GpuTensorHandle tensorView;
    res = gpuCreateTensorView(device, tensorA, &viewDesc, &tensorView);
    if (res != GPU_SUCCESS) {
        printf("Failed to create tensor view: %d\n", res);
    } else {
        printf("Successfully created 1D view of 2D tensor\n");
        printf("View is contiguous: %s\n", gpuTensorIsContiguous(device, tensorView) ? "YES" : "NO");

        // Test view data access
        float viewElement;
        res = gpuDownloadTensor(device, tensorView, &viewElement, sizeof(float));
        if (res == GPU_SUCCESS) {
            printf("View element[0] = %.0f (expected 0)\n", viewElement);
        }

        gpuDestroyTensor(device, tensorView);
    }

    // Test 5: Get tensor descriptor
    printf("\n--- Test 5: Tensor Descriptor ---\n");
    GpuTensorDesc retrievedDesc;
    res = gpuGetTensorDesc(device, tensorA, &retrievedDesc);
    if (res == GPU_SUCCESS) {
        printf("Retrieved descriptor:\n");
        printf("  Format: %s\n", gpuTensorFormatToString(retrievedDesc.format));
        printf("  Dimensions: %d\n", retrievedDesc.dimCount);
        printf("  Shape: [%d, %d]\n", retrievedDesc.dims[0], retrievedDesc.dims[1]);
        printf("  Strides: [%d, %d]\n", retrievedDesc.strides[0], retrievedDesc.strides[1]);
    }

    // Cleanup
    printf("\n--- Cleanup ---\n");
    free(uploadData);
    free(downloadData);
    gpuDestroyTensor(device, tensorA);
    gpuDestroyTensor(device, tensorB);
    gpuDestroyDevice(device);
    gpuPlatformShutdown();

    printf("\n=== All Tensor Data Tests Completed ===\n");
    return 0;
}
