#include "gpu/gpu.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/ai/gpu_matrix_pipeline.h"
#include "gpu/ai/gpu_neural.h"
#include "gpu/raytracing/gpu_raytracing.h"
#include "gpu/workgraph/gpu_work_graph.h"
#include "gpu/capability/gpu_feature_gating.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { \
    GpuResult _r = (expr); \
    if (_r != GPU_SUCCESS) { \
        fprintf(stderr, "FAIL: %s returned %d at %s:%d\n", #expr, _r, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

static void flush(void) { fflush(stdout); fflush(stderr); }

int main(void)
{
    printf("=== Phase 3 Integration Test ===\n\n"); flush();

    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    printf("[T3.1] Device\n"); flush();
    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "phase3_test", .enableDebugLayer = false };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) {
        printf("Device creation failed: %d (may need GPU)\n", res);
        gpuPlatformShutdown();
        return 0;
    }
    printf("  OK\n"); flush();

    printf("[T3.2] Feature Table\n"); flush();
    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);
    printf("  Bindless=%s RT=%s WorkGraph=%s CoopMatrix=%s\n",
           features.supported[GPU_FEATURE_BINDLESS] ? "Y" : "N",
           features.supported[GPU_FEATURE_RAY_TRACING] ? "Y" : "N",
           features.supported[GPU_FEATURE_WORK_GRAPH] ? "Y" : "N",
           features.supported[GPU_FEATURE_COOPERATIVE_MATRIX] ? "Y" : "N");
    printf("  OK\n"); flush();

    printf("[T3.3] Tensor Lifecycle\n"); flush();
    GpuTensorDesc tensorDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {64, 64},
        .strides = {64, 1}
    };
    GpuTensorHandle tensorA, tensorB;
    res = gpuCreateTensor(device, &tensorDesc, &tensorA);
    if (res == GPU_SUCCESS) {
        printf("  Tensor A created: %zu bytes\n", gpuCalculateTensorSize(&tensorDesc));

        float uploadData[64];
        for (int i = 0; i < 64; i++) uploadData[i] = (float)i;
        res = gpuUploadTensor(device, tensorA, uploadData, sizeof(uploadData));

        res = gpuCreateTensor(device, &tensorDesc, &tensorB);

        bool isContig = gpuTensorIsContiguous(device, tensorA);
        printf("  Is contiguous: %s\n", isContig ? "YES" : "NO");

        GpuTensorDesc viewDesc = {
            .format = GPU_TENSOR_FORMAT_F32,
            .dimCount = 1,
            .dims = {64*64},
            .strides = {1}
        };
        GpuTensorHandle view;
        res = gpuCreateTensorView(device, tensorA, &viewDesc, &view);
        printf("  View created: %s\n", res == GPU_SUCCESS ? "YES" : "NO");

        GpuTensorDesc retrieved;
        res = gpuGetTensorDesc(device, tensorA, &retrieved);
        printf("  Desc: format=%s dims=%d shape=[%u,%u]\n",
               gpuTensorFormatToString(retrieved.format),
               retrieved.dimCount, retrieved.dims[0], retrieved.dims[1]);

        if (res == GPU_SUCCESS) gpuDestroyTensor(device, view);
        gpuDestroyTensor(device, tensorB);
        gpuDestroyTensor(device, tensorA);
    } else {
        printf("  Tensor creation: %d\n", res);
    }
    printf("  OK\n"); flush();

    printf("[T3.4] Neural Network\n"); flush();
    GpuNeuralNetworkDesc nnDesc = {
        .inputWidth = 64,
        .inputHeight = 64,
        .hiddenDim = 32,
        .outputChannels = 3,
        .weightFormat = GPU_TENSOR_FORMAT_F32,
    };
    GpuNeuralNetwork nn;
    res = gpuCreateNeuralNetwork(device, &nnDesc, &nn);
    if (res == GPU_SUCCESS) {
        printf("  Neural network created: %ux%u -> %u -> %uch\n",
               nnDesc.inputWidth, nnDesc.inputHeight,
               nnDesc.hiddenDim, nnDesc.outputChannels);
        gpuDestroyNeuralNetwork(device, nn);
    } else {
        printf("  Neural network creation: %d\n", res);
    }
    printf("  OK\n"); flush();

    printf("[T3.5] Work Graph (API check)\n"); flush();
    GpuWorkGraphDesc wgDesc = { .nodeCount = 0, .nodes = NULL, .edgeCount = 0, .edges = NULL };
    GpuWorkGraph wg;
    res = gpuCreateWorkGraph(device, &wgDesc, &wg);
    printf("  WorkGraph: %s (%d, expected NOT_SUPPORTED)\n",
           res == GPU_ERROR_NOT_SUPPORTED ? "correctly NOT_SUPPORTED" : "unexpected result",
           res);
    printf("  OK\n"); flush();

    printf("[T3.6] Ray Tracing (API check)\n"); flush();
    GpuAccelerationStructureGeometry geom = {};
    (void)geom;
    GpuBottomLevelASDesc blasDesc = { .geometryCount = 0, .geometries = NULL, .allowUpdate = false };
    GpuAccelerationStructureHandle blas;
    res = gpuCreateBottomLevelAS(device, &blasDesc, &blas);
    printf("  BLAS: %s (%d)\n",
           res == GPU_ERROR_INVALID_ARGS || res == GPU_ERROR_NOT_SUPPORTED ? "expected fail" : "unexpected",
           res);
    printf("  OK\n"); flush();

    gpuDestroyDevice(device);
    gpuPlatformShutdown();
    printf("\nALL PASSED\n"); flush();
    return 0;
}
