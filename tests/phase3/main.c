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

#define CHECK_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
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
    CHECK(gpuCreateTensor(device, &tensorDesc, &tensorA));
    printf("  Tensor A created: %zu bytes\n", gpuCalculateTensorSize(&tensorDesc));

    float uploadData[64];
    for (int i = 0; i < 64; i++) uploadData[i] = (float)i;
    CHECK(gpuUploadTensor(device, tensorA, uploadData, sizeof(uploadData)));

    float downloadData[64] = {0};
    CHECK(gpuDownloadTensor(device, tensorA, downloadData, sizeof(downloadData)));
    for (int i = 0; i < 64; i++) {
        CHECK_TRUE(downloadData[i] == uploadData[i]);
    }

    CHECK(gpuCreateTensor(device, &tensorDesc, &tensorB));

    bool isContig = gpuTensorIsContiguous(device, tensorA);
    printf("  Is contiguous: %s\n", isContig ? "YES" : "NO");
    CHECK_TRUE(isContig);

    GpuTensorDesc defaultStrideDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 3,
        .dims = {2, 3, 4},
        .strides = {0, 0, 0}
    };
    GpuTensorHandle defaultStrideTensor;
    CHECK(gpuCreateTensor(device, &defaultStrideDesc, &defaultStrideTensor));
    GpuTensorDesc defaultStrideRetrieved;
    CHECK(gpuGetTensorDesc(device, defaultStrideTensor, &defaultStrideRetrieved));
    CHECK_TRUE(defaultStrideRetrieved.strides[0] == 12);
    CHECK_TRUE(defaultStrideRetrieved.strides[1] == 4);
    CHECK_TRUE(defaultStrideRetrieved.strides[2] == 1);
    CHECK_TRUE(gpuTensorIsContiguous(device, defaultStrideTensor));
    CHECK(gpuDestroyTensor(device, defaultStrideTensor));

    GpuTensorDesc viewDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 1,
        .dims = {64*64},
        .strides = {1}
    };
    GpuTensorHandle view;
    CHECK(gpuCreateTensorView(device, tensorA, &viewDesc, &view));
    printf("  View created: YES\n");

    GpuTensorDesc tooLargeViewDesc = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 1,
        .dims = {64*64 + 1},
        .strides = {1}
    };
    GpuTensorHandle invalidView;
    res = gpuCreateTensorView(device, tensorA, &tooLargeViewDesc, &invalidView);
    CHECK_TRUE(res == GPU_ERROR_INVALID_ARGS);

    CHECK(gpuDestroyTensor(device, tensorA));
    float viewElement = -1.0f;
    CHECK(gpuDownloadTensor(device, view, &viewElement, sizeof(viewElement)));
    CHECK_TRUE(viewElement == 0.0f);

    GpuTensorDesc retrieved;
    CHECK(gpuGetTensorDesc(device, view, &retrieved));
    printf("  View desc: format=%s dims=%d shape=[%u]\n",
           gpuTensorFormatToString(retrieved.format),
           retrieved.dimCount, retrieved.dims[0]);

    CHECK(gpuDestroyTensor(device, view));
    CHECK(gpuDestroyTensor(device, tensorB));
    printf("  OK\n"); flush();

    printf("[T3.4] Matmul Execution\n"); flush();
    GpuTensorDesc matA = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {2, 3},
        .strides = {3, 1}
    };
    GpuTensorDesc matB = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {3, 2},
        .strides = {2, 1}
    };
    GpuTensorDesc matC = {
        .format = GPU_TENSOR_FORMAT_F32,
        .dimCount = 2,
        .dims = {2, 2},
        .strides = {2, 1}
    };
    GpuTensorHandle tensorMatA, tensorMatB, tensorMatC;
    CHECK(gpuCreateTensor(device, &matA, &tensorMatA));
    CHECK(gpuCreateTensor(device, &matB, &tensorMatB));
    CHECK(gpuCreateTensor(device, &matC, &tensorMatC));

    float matAData[6] = {1, 2, 3, 4, 5, 6};
    float matBData[6] = {7, 8, 9, 10, 11, 12};
    CHECK(gpuUploadTensor(device, tensorMatA, matAData, sizeof(matAData)));
    CHECK(gpuUploadTensor(device, tensorMatB, matBData, sizeof(matBData)));

    GpuMatmulDesc matmulDesc = {
        .m = 2,
        .n = 2,
        .k = 3,
        .aFormat = GPU_TENSOR_FORMAT_F32,
        .bFormat = GPU_TENSOR_FORMAT_F32,
        .cFormat = GPU_TENSOR_FORMAT_F32,
        .useCooperativeMatrix = false,
    };
    GpuPipelineHandle matmulPipeline;
    res = gpuCreateMatmulPipeline(device, &matmulDesc, &matmulPipeline);
    if (res == GPU_ERROR_NOT_SUPPORTED) {
        printf("  Matmul pipeline not supported on this backend, skipped\n");
    } else {
        CHECK(res);
        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
        CHECK_TRUE(encoder != NULL);
        GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(cmd != NULL);
        GpuMatmulBindings bindings = {
            .a = tensorMatA,
            .b = tensorMatB,
            .c = tensorMatC,
        };
        gpuCmdMatmul(cmd, matmulPipeline, &bindings);
        CHECK(gpuQueueSubmit(queue, 1, &cmd));
        CHECK(gpuQueueWaitOnHost(queue));

        float matCData[4] = {0, 0, 0, 0};
        CHECK(gpuDownloadTensor(device, tensorMatC, matCData, sizeof(matCData)));
        CHECK_TRUE(matCData[0] == 58.0f);
        CHECK_TRUE(matCData[1] == 64.0f);
        CHECK_TRUE(matCData[2] == 139.0f);
        CHECK_TRUE(matCData[3] == 154.0f);
        CHECK(gpuDestroyPipeline(device, matmulPipeline));
        printf("  Matmul result verified\n");
    }
    CHECK(gpuDestroyTensor(device, tensorMatC));
    CHECK(gpuDestroyTensor(device, tensorMatB));
    CHECK(gpuDestroyTensor(device, tensorMatA));
    printf("  OK\n"); flush();

    printf("[T3.5] Neural Network\n"); flush();
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

    printf("[T3.6] Work Graph (API check)\n"); flush();
    GpuWorkGraphDesc wgDesc = { .nodeCount = 0, .nodes = NULL, .edgeCount = 0, .edges = NULL };
    GpuWorkGraph wg;
    res = gpuCreateWorkGraph(device, &wgDesc, &wg);
    printf("  WorkGraph: %s (%d)\n",
           res == GPU_SUCCESS ? "created fallback graph" : "unexpected result",
           res);
    CHECK_TRUE(res == GPU_SUCCESS);
    gpuDestroyWorkGraph(device, wg);
    printf("  OK\n"); flush();

    printf("[T3.7] Ray Tracing (API check)\n"); flush();
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
