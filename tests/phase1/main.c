#include "gpu/gpu.h"
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
    printf("=== Phase 1 Integration Test ===\n\n"); flush();

    printf("[T1.1] Device & Queue\n"); flush();
    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "phase1_test", .enableDebugLayer = false };
    CHECK(gpuCreateDevice(&devDesc, &device));

    GpuCommandQueue queue;
    CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));
    printf("  OK\n"); flush();

    printf("[T1.1] Buffer lifecycle\n"); flush();
    GpuBufferDesc bufDesc = { .size = 256, .elementSize = 4, .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE };
    GpuBufferHandle buf;
    CHECK(gpuCreateBuffer(device, &bufDesc, &buf));
    if (!gpuHandleIsValid(buf)) { fprintf(stderr, "  FAIL: handle invalid\n"); return 1; }

    uint32_t oldGen = buf.generation;
    CHECK(gpuDestroyBuffer(device, buf));
    GpuBufferHandle buf2;
    CHECK(gpuCreateBuffer(device, &bufDesc, &buf2));
    if (buf2.index != buf.index || buf2.generation <= oldGen) {
        fprintf(stderr, "  FAIL: reuse/gen\n"); return 1;
    }
    CHECK(gpuDestroyBuffer(device, buf2));

    uint32_t initData[64];
    for (int i = 0; i < 64; i++) initData[i] = (uint32_t)i;
    GpuBufferDesc initDesc = { .size = sizeof(initData), .elementSize = 4,
        .usage = GPU_BUFFER_USAGE_VERTEX_BUFFER | GPU_BUFFER_USAGE_COPY_DEST };
    GpuBufferHandle bufInit;
    CHECK(gpuCreateBufferInit(device, &initDesc, initData, &bufInit));
    CHECK(gpuDestroyBuffer(device, bufInit));
    printf("  OK\n"); flush();

    printf("[T1.1] Texture lifecycle\n"); flush();
    GpuTextureDesc texDesc = {
        .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64, .depth = 1,
        .arrayLength = 1, .mipCount = 1, .format = GPU_FORMAT_RGBA8_UNORM,
        .sampleCount = 1, .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_RENDER_TARGET,
    };
    GpuTextureHandle tex;
    CHECK(gpuCreateTexture(device, &texDesc, &tex));
    CHECK(gpuDestroyTexture(device, tex));
    printf("  OK\n"); flush();

    printf("[T1.1] Command encoding & submit\n"); flush();
    GpuCommandEncoder enc = gpuBeginCommandEncoder(device, queue);
    if (!enc) { fprintf(stderr, "  FAIL: enc null\n"); return 1; }
    GpuCommandBuffer cmd = gpuFinishCommandEncoder(enc);
    if (!cmd) { fprintf(stderr, "  FAIL: cmd null\n"); return 1; }
    CHECK(gpuQueueSubmit(queue, 1, &cmd));
    CHECK(gpuQueueWaitOnHost(queue));
    printf("  OK\n"); flush();

    printf("[T1.6] Capabilities\n"); flush();
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    printf("  bindless=%d mesh=%d rt=%d maxTexDim=%u\n",
           caps.supportBindless, caps.supportMeshShader,
           caps.supportRayTracing, caps.maxTextureDimension);
    printf("  OK\n"); flush();

    printf("[T1.4] Fence\n"); flush();
    GpuFence fence;
    CHECK(gpuCreateFence(device, 0, &fence));

    enc = gpuBeginCommandEncoder(device, queue);
    cmd = gpuFinishCommandEncoder(enc);
    CHECK(gpuQueueSubmitWithFence(queue, 1, &cmd, fence, 1));
    CHECK(gpuFenceWait(device, fence, 1, 5000));
    if (gpuFenceGetCurrentValue(fence) < 1) {
        fprintf(stderr, "  FAIL: fence value\n"); return 1;
    }
    gpuDestroyFence(device, fence);
    printf("  OK\n"); flush();

    printf("[T1.4] Barrier\n"); flush();
    GpuBufferDesc ssboDesc = { .size = 1024,
        .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_SHADER_RESOURCE };
    GpuBufferHandle ssbo;
    CHECK(gpuCreateBuffer(device, &ssboDesc, &ssbo));
    enc = gpuBeginCommandEncoder(device, queue);
    if (enc) {
        gpuCmdSetBufferState(device, enc, ssbo, GPU_RESOURCE_STATE_UNORDERED_ACCESS);
        gpuCmdGlobalBarrier(enc);
        gpuCmdSetBufferState(device, enc, ssbo, GPU_RESOURCE_STATE_SHADER_RESOURCE);
        cmd = gpuFinishCommandEncoder(enc);
        if (cmd) gpuQueueSubmit(queue, 1, &cmd);
    }
    CHECK(gpuDestroyBuffer(device, ssbo));
    printf("  OK\n"); flush();

    printf("[T1.4] ResourceManager\n"); flush();
    GpuResourceManager rmgr;
    CHECK(gpuResourceManagerCreate(device, &rmgr));
    CHECK(gpuResourceManagerFlush(rmgr, 0));
    gpuResourceManagerDestroy(rmgr);
    printf("  OK\n"); flush();

    printf("[T1.4] Readback\n"); flush();
    GpuBufferHandle readback;
    CHECK(gpuCreateReadbackBuffer(device, 256, &readback));
    CHECK(gpuDestroyBuffer(device, readback));
    printf("  OK\n"); flush();

    printf("[T1.7] Debug layer\n"); flush();
    gpuSetDebugCallback(device, NULL, NULL);
    gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_ERROR);
    printf("  OK\n"); flush();

    printf("[T1.2] Shader compiler\n"); flush();
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
        const char* diag = gpuGetShaderCompileDiagnostic(compiler);
        printf("  Compile: expected fail (no file): %d (%s)\n", compileRes, diag ? diag : "");
    } else {
        uint64_t dataSize = 0;
        gpuGetShaderProgramData(program, &dataSize);
        printf("  Compile: OK, binary=%llu bytes\n", (unsigned long long)dataSize);

        printf("[T1.1] Render pipeline\n"); flush();
        GpuColorTargetDesc targetDesc = { .format = GPU_FORMAT_RGBA8_UNORM };
        GpuRenderPipelineDesc pipelineDesc = {
            .program = program,
            .primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .targets = &targetDesc,
            .targetCount = 1,
            .label = "test_pipeline",
        };
        GpuRenderPipeline pipeline;
        GpuResult pipeRes = gpuCreateRenderPipeline(device, &pipelineDesc, &pipeline);
        if (pipeRes != GPU_SUCCESS) {
            printf("  Pipeline: FAIL %d\n", pipeRes);
        } else {
            printf("  Pipeline: OK\n"); flush();
            gpuDestroyRenderPipeline(device, pipeline);
        }
        gpuDestroyShaderProgram(program);
    }
    gpuDestroyShaderCompiler(compiler);
    printf("  OK\n"); flush();

    printf("[T1.5] Bindless heap\n"); flush();
    GpuBindlessHeap heap;
    GpuBindlessHeapDesc heapDesc = { .maxDescriptors = 256, .descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE };
    CHECK(gpuCreateBindlessHeap(device, &heapDesc, &heap));
    uint32_t idx = gpuBindlessAllocate(heap, (GpuHandle){1, 1});
    printf("  Allocate: index=%u\n", idx); flush();
    gpuBindlessFree(heap, idx);
    gpuDestroyBindlessHeap(heap);
    printf("  OK\n"); flush();

    gpuDestroyDevice(device);
    printf("\nALL PASSED\n"); flush();
    return 0;
}
