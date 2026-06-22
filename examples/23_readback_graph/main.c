#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RT_SIZE 256

typedef struct {
    GpuRenderPipeline pipeline;
    int renderCalled;
    int copyCalled;
} GraphData;

static void render_cb(GpuGraphPassContext* ctx, void* ud)
{
    GraphData* d = (GraphData*)ud;
    d->renderCalled = 1;
    if (ctx->renderPass && d->pipeline) {
        gpuCmdBindRenderPipeline(ctx->renderPass, d->pipeline);
        gpuCmdSetViewport(ctx->renderPass, 0.0f, 0.0f, (float)RT_SIZE, (float)RT_SIZE);
        gpuCmdDraw(ctx->renderPass, 3, 1, 0, 0);
    }
}

static void copy_cb(GpuGraphPassContext* ctx, void* ud)
{
    GraphData* d = (GraphData*)ud;
    d->copyCalled = 1;
    (void)ctx;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    GpuResult res;

    printf("=== Render Graph Readback Test ===\n");

    res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("FAIL: Platform init: %d\n", res); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "23_readback_graph", .enableDebugLayer = false };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("FAIL: Device: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("FAIL: Queue: %d\n", res); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }

    GpuTextureDesc texDesc = {
        .type = GPU_TEXTURE_TYPE_2D, .width = RT_SIZE, .height = RT_SIZE,
        .depth = 1, .arrayLength = 1, .mipCount = 1,
        .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
        .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_COPY_SOURCE,
        .label = "rt",
    };
    GpuTextureHandle rt = {0, 0};
    res = gpuCreateTexture(device, &texDesc, &rt);
    if (res != GPU_SUCCESS) { printf("FAIL: Texture: %d\n", res); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }
    printf("[1] Texture OK\n");

    GpuShaderCompiler compiler = NULL;
    res = gpuCreateShaderCompiler(device, &compiler);
    if (res != GPU_SUCCESS) { printf("FAIL: Compiler: %d\n", res); gpuDestroyTexture(device, rt); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }

    GpuShaderCompileDesc cdesc = {
        .sourcePath = "triangle.slang",
        .entryPoint = "vertexMain",
        .fragmentEntryPoint = "fragmentMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderProgram prog = NULL;
    res = gpuCompileShader(compiler, &cdesc, &prog);
    if (res != GPU_SUCCESS) {
        printf("FAIL: Shader: %d (%s)\n", res, gpuGetShaderCompileDiagnostic(compiler));
        gpuDestroyShaderCompiler(compiler); gpuDestroyTexture(device, rt); gpuDestroyDevice(device); gpuPlatformShutdown();
        return 1;
    }
    printf("[2] Shader OK\n");

    GpuColorTargetDesc tdesc = { .format = GPU_FORMAT_RGBA8_UNORM };
    GpuRenderPipelineDesc pdesc = {
        .program = prog, .primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .targets = &tdesc, .targetCount = 1, .label = "tri",
    };
    GpuRenderPipeline pipeline = NULL;
    res = gpuCreateRenderPipeline(device, &pdesc, &pipeline);
    if (res != GPU_SUCCESS) { printf("FAIL: Pipeline: %d\n", res); gpuDestroyShaderProgram(prog); gpuDestroyShaderCompiler(compiler); gpuDestroyTexture(device, rt); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }
    printf("[3] Pipeline OK\n");

    const uint32_t bufSize = RT_SIZE * RT_SIZE * 4;
    GpuBufferHandle readbackBuf = {0, 0};
    res = gpuCreateReadbackBuffer(device, bufSize, &readbackBuf);
    if (res != GPU_SUCCESS) { printf("FAIL: Readback buffer: %d\n", res); gpuDestroyRenderPipeline(device, pipeline); gpuDestroyShaderProgram(prog); gpuDestroyShaderCompiler(compiler); gpuDestroyTexture(device, rt); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }
    printf("[4] Readback buffer OK\n");

    int passed = 0;

    GpuGraph graph;
    res = gpuGraphCreate(device, &graph);
    if (res != GPU_SUCCESS) { printf("FAIL: Graph create: %d\n", res); goto cleanup; }

    {
        GraphData gd = { .pipeline = pipeline, .renderCalled = 0, .copyCalled = 0 };
        GpuGraphResource rtRes = gpuGraphImportTexture(graph, rt, GPU_RESOURCE_STATE_RENDER_TARGET, "rt");

        GpuGraphPass renderPass = gpuGraphAddRenderPass(graph, "render");
        GpuGraphColorAttachment ca = {
            .resource = rtRes,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 1.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(renderPass, 1, &ca);
        gpuGraphPassSetCallback(renderPass, render_cb, &gd);

        GpuGraphPass copyPass = gpuGraphAddCopyPass(graph, "copy");
        gpuGraphPassRead(copyPass, rtRes);
        gpuGraphPassSetCallback(copyPass, copy_cb, &gd);

        res = gpuGraphCompile(graph);
        if (res != GPU_SUCCESS) {
            printf("FAIL: Graph compile: %d\n", res);
            gpuGraphDestroy(graph);
            goto cleanup;
        }
        printf("[5] Graph compiled OK (passes=%u)\n", gpuGraphGetPassCount(graph));

        res = gpuGraphExecute(graph, queue);
        if (res != GPU_SUCCESS) {
            printf("FAIL: Graph execute: %d\n", res);
            gpuGraphDestroy(graph);
            goto cleanup;
        }
        printf("[6] Graph executed OK (renderCalled=%d copyCalled=%d)\n", gd.renderCalled, gd.copyCalled);

        if (gd.renderCalled && gd.copyCalled) {
            passed = 1;
        }
    }
    gpuGraphDestroy(graph);
    gpuQueueWaitOnHost(queue);

    if (passed) {
        printf("\n=== PASS: Render graph executed render + copy passes ===\n");
    } else {
        printf("\n=== FAIL: Pass callbacks not called ===\n");
    }

cleanup:
    gpuDestroyBuffer(device, readbackBuf);
    gpuDestroyRenderPipeline(device, pipeline);
    gpuDestroyShaderProgram(prog);
    gpuDestroyShaderCompiler(compiler);
    gpuDestroyTexture(device, rt);
    gpuDestroyDevice(device);
    gpuPlatformShutdown();

    return passed ? 0 : 1;
}
