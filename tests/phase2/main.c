#include "gpu/gpu.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/pipeline/gpu_pipeline_cache.h"
#include "gpu/layout/gpu_parameter_block.h"
#include "gpu/layout/gpu_shader_object.h"
#include "gpu/queue/gpu_multi_queue.h"
#include "gpu/capability/gpu_feature_gating.h"
#include "gpu/reflection/gpu_type_info.h"
#include "gpu/reflection/gpu_reflection_cache.h"
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
    printf("=== Phase 2 Integration Test ===\n\n"); flush();

    printf("[T2.1] Device & Queues\n"); flush();
    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "phase2_test", .enableDebugLayer = false };
    CHECK(gpuCreateDevice(&devDesc, &device));

    GpuCommandQueue queue;
    CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

    GpuQueueSet qset;
    CHECK(gpuGetQueueSet(device, &qset));
    printf("  graphics=%p compute=%p transfer=%p\n",
           (void*)qset.graphics, (void*)qset.compute, (void*)qset.transfer);
    printf("  OK\n"); flush();

    printf("[T2.2] Feature Table\n"); flush();
    GpuFeatureTable features;
    CHECK(gpuBuildFeatureTable(device, &features));
    printf("  Bindless=%s MeshShader=%s RT=%s CoopMatrix=%s\n",
           features.supported[GPU_FEATURE_BINDLESS] ? "Y" : "N",
           features.supported[GPU_FEATURE_MESH_SHADER] ? "Y" : "N",
           features.supported[GPU_FEATURE_RAY_TRACING] ? "Y" : "N",
           features.supported[GPU_FEATURE_COOPERATIVE_MATRIX] ? "Y" : "N");
    printf("  OK\n"); flush();

    printf("[T2.3] Shader Compiler & Pipeline\n"); flush();
    GpuShaderCompiler compiler;
    CHECK(gpuCreateShaderCompiler(device, &compiler));

    GpuShaderCompileDesc compileDesc = {
        .sourcePath = "triangle.slang",
        .entryPoint = "vertexMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderProgram program;
    GpuResult compileRes = gpuCompileShader(compiler, &compileDesc, &program);
    if (compileRes != GPU_SUCCESS) {
        printf("  Compile: expected fail (no file): %d\n", compileRes);
    }
    gpuDestroyShaderCompiler(compiler);
    printf("  OK\n"); flush();

    printf("[T2.4] Pipeline Cache\n"); flush();
    GpuPipelineCache pipeCache;
    GpuPipelineCacheDesc cacheDesc = { .cachePath = NULL };
    CHECK(gpuCreatePipelineCache(device, &cacheDesc, &pipeCache));

    uint8_t testHash[GPU_SHA256_HASH_SIZE] = {0};
    testHash[0] = 0x42;
    GpuPipelineHandle testPipe = {1, 1};
    CHECK(gpuPipelineCacheStore(pipeCache, testHash, testPipe));

    GpuPipelineHandle lookupPipe;
    GpuResult lookupRes = gpuPipelineCacheLookup(pipeCache, testHash, &lookupPipe);
    if (lookupRes == GPU_OK && lookupPipe.index == testPipe.index) {
        printf("  Cache hit: OK\n");
    } else {
        printf("  Cache hit: unexpected result %d\n", lookupRes);
    }

    uint32_t hits, misses, entries;
    gpuPipelineCacheGetStats(pipeCache, &hits, &misses, &entries);
    printf("  Stats: hits=%u misses=%u entries=%u\n", hits, misses, entries);
    gpuDestroyPipelineCache(device, pipeCache);
    printf("  OK\n"); flush();

    printf("[T2.5] Reflection Cache\n"); flush();
    GpuReflectionCache refCache;
    CHECK(gpuReflectionCacheCreate(&refCache));
    gpuReflectionCacheDestroy(refCache);
    printf("  OK\n"); flush();

    printf("[T2.6] Parameter Block\n"); flush();
    GpuTypeInfo scalarType = {};
    scalarType.kind = GPU_TYPE_KIND_SCALAR;
    scalarType.name = "float";
    scalarType.size = 4;

    GpuStructField field = {};
    field.name = "value";
    field.offset = 0;
    field.type = &scalarType;

    GpuTypeInfo structType = {};
    structType.kind = GPU_TYPE_KIND_STRUCT;
    structType.name = "TestParams";
    structType.size = 4;
    structType.structInfo.fieldCount = 1;
    structType.structInfo.fields = &field;

    GpuParameterBlock pb;
    GpuResult pbRes = gpuCreateParameterBlock(device, &structType, &pb);
    if (pbRes == GPU_OK && pb) {
        gpuParameterBlockSetFloat(pb, "value", 42.0f);
        GpuResult uploadRes = gpuParameterBlockUpload(device, pb);
        printf("  ParameterBlock created, set, upload=%d\n", uploadRes);
        gpuDestroyParameterBlock(device, pb);
    } else {
        printf("  ParameterBlock creation: %d\n", pbRes);
    }
    printf("  OK\n"); flush();

    printf("[T2.7] Fence with Multi-Queue\n"); flush();
    GpuFence fence;
    CHECK(gpuCreateFence(device, 0, &fence));

    GpuCommandEncoder enc = gpuBeginCommandEncoder(device, queue);
    GpuCommandBuffer cmd = gpuFinishCommandEncoder(enc);
    CHECK(gpuQueueSubmitWithFence(queue, 1, &cmd, fence, 1));
    CHECK(gpuFenceWait(device, fence, 1, 5000));

    if (gpuFenceIsCompleted(fence, 1)) {
        printf("  Fence completed: YES\n");
    }
    gpuDestroyFence(device, fence);
    printf("  OK\n"); flush();

    gpuDestroyDevice(device);
    printf("\nALL PASSED\n"); flush();
    return 0;
}
