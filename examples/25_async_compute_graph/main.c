// 25_async_compute_graph - Render graph with compute + graphics passes.
// Demonstrates MULTI_QUEUE scheduling when the backend exposes independent queues.

#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    GpuRenderPipeline pipeline;
    GpuComputePipeline computePipeline;
    GpuComputeBinding computeBinding;
    GpuBufferHandle counterBuffer;
} TriangleGraphData;

static void compute_prepass_callback(GpuGraphPassContext* ctx, void* userData)
{
    TriangleGraphData* data = (TriangleGraphData*)userData;
    if (ctx->computePass && data->computeBinding && data->counterBuffer.index != 0) {
        gpuComputeBindingDispatch(data->computeBinding, ctx->computePass,
                                  "gCounter", data->counterBuffer, 1, 1, 1);
    }
}

static void triangle_callback(GpuGraphPassContext* ctx, void* userData)
{
    TriangleGraphData* data = (TriangleGraphData*)userData;
    if (ctx->renderPass && data->pipeline) {
        gpuCmdBindRenderPipeline(ctx->renderPass, data->pipeline);
        gpuCmdSetViewport(ctx->renderPass, 0.0f, 0.0f, 800.0f, 600.0f);
        gpuCmdDraw(ctx->renderPass, 3, 1, 0, 0);
    }
}

static void print_queue_caps(GpuDevice device)
{
    GpuQueueInfo graphics = {}, compute = {}, transfer = {};
    gpuGetQueueInfo(device, GPU_QUEUE_TYPE_GRAPHICS, &graphics);
    gpuGetQueueInfo(device, GPU_QUEUE_TYPE_COMPUTE, &compute);
    gpuGetQueueInfo(device, GPU_QUEUE_TYPE_TRANSFER, &transfer);

    printf("Queue capabilities:\n");
    printf("  graphics: support=%d (%s)\n", graphics.support, graphics.reason);
    printf("  compute:  support=%d (%s)\n", compute.support, compute.reason);
    printf("  transfer: support=%d (%s)\n", transfer.support, transfer.reason);
    printf("  independent queues: %s\n",
           gpuDeviceSupportsIndependentQueues(device) ? "yes" : "no (alias graphics)");
    if (gpuDeviceSupportsIndependentQueues(device)) {
        printf("  MULTI_QUEUE will use dedicated graphics/compute/transfer queues\n");
    } else {
        printf("  MULTI_QUEUE will downgrade to single command buffer\n");
    }
}

int main(void)
{
    GpuResult res;
    res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuWindow window = NULL;
    GpuWindowDesc winDesc = {
        .title = "25_async_compute_graph",
        .width = 800,
        .height = 600,
        .vsync = true,
        .resizable = true,
    };
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) { gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "25_async_compute_graph", .enableDebugLayer = false };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    print_queue_caps(device);

    GpuSurface surface = NULL;
    res = gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    if (res != GPU_SUCCESS) { gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuFormat fmt = gpuSurfaceGetPreferredFormat(surface);
    res = gpuSurfaceConfigure(surface, 800, 600, fmt, true);
    if (res != GPU_SUCCESS) { gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuShaderCompiler compiler = NULL;
    res = gpuCreateShaderCompiler(device, &compiler);
    if (res != GPU_SUCCESS) { gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuShaderCompileDesc compileDesc = {
        .sourcePath = "triangle.slang",
        .entryPoint = "vertexMain",
        .fragmentEntryPoint = "fragmentMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderProgram program = NULL;
    res = gpuCompileShader(compiler, &compileDesc, &program);
    if (res != GPU_SUCCESS) {
        printf("Shader compile failed: %s\n", gpuGetShaderCompileDiagnostic(compiler));
        gpuDestroyShaderCompiler(compiler);
        gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown();
        return 1;
    }

    GpuColorTargetDesc targetDesc = { .format = fmt };
    GpuRenderPipelineDesc pipelineDesc = {
        .program = program,
        .primitiveTopology = GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .targets = &targetDesc,
        .targetCount = 1,
        .label = "triangle_pipeline",
    };
    GpuRenderPipeline pipeline = NULL;
    res = gpuCreateRenderPipeline(device, &pipelineDesc, &pipeline);
    if (res != GPU_SUCCESS) { gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuShaderCompileDesc fillCompileDesc = {
        .sourcePath = "fill.slang",
        .entryPoint = "fillMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderProgram fillProgram = NULL;
    res = gpuCompileShader(compiler, &fillCompileDesc, &fillProgram);
    if (res != GPU_SUCCESS) {
        printf("Fill shader compile failed: %s\n", gpuGetShaderCompileDiagnostic(compiler));
        gpuDestroyRenderPipeline(device, pipeline);
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown();
        return 1;
    }

    GpuComputePipelineDesc fillPipelineDesc = {
        .program = fillProgram,
        .label = "fill_pipeline",
    };
    GpuComputePipeline computePipeline = NULL;
    res = gpuCreateComputePipeline(device, &fillPipelineDesc, &computePipeline);
    if (res != GPU_SUCCESS) {
        gpuDestroyShaderProgram(fillProgram);
        gpuDestroyRenderPipeline(device, pipeline);
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown();
        return 1;
    }

    GpuComputeBinding computeBinding = NULL;
    res = gpuCreateComputeBinding(device, computePipeline, &computeBinding);
    if (res != GPU_SUCCESS) {
        gpuDestroyComputePipeline(device, computePipeline);
        gpuDestroyShaderProgram(fillProgram);
        gpuDestroyRenderPipeline(device, pipeline);
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown();
        return 1;
    }

    TriangleGraphData graphData = {
        .pipeline = pipeline,
        .computePipeline = computePipeline,
        .computeBinding = computeBinding,
    };

    GpuBufferDesc counterDesc = {
        .size = 4,
        .elementSize = 4,
        .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS,
        .label = "frame_counter",
    };
    GpuBufferHandle counterBuffer;
    res = gpuCreateBuffer(device, &counterDesc, &counterBuffer);
    if (res != GPU_SUCCESS) { gpuDestroyComputeBinding(computeBinding); gpuDestroyComputePipeline(device, computePipeline); gpuDestroyShaderProgram(fillProgram); gpuDestroyRenderPipeline(device, pipeline); gpuDestroyShaderProgram(program); gpuDestroyShaderCompiler(compiler); gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }
    graphData.counterBuffer = counterBuffer;

    printf("Rendering 120 frames (compute prepass + triangle, MULTI_QUEUE request)...\n");

    uint32_t frameCount = 0;
    GpuPlatformEvent ev;
    int quit = 0;
    while (frameCount < 120 && !quit) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) { quit = 1; break; }
            if (ev.type == GPU_PLATFORM_EVENT_RESIZE)
                gpuSurfaceConfigure(surface, ev.resize.width, ev.resize.height, fmt, true);
        }
        if (quit) break;

        GpuSurfaceTexture backbuffer = NULL;
        res = gpuSurfaceAcquireNextImage(surface, &backbuffer);
        if (res != GPU_SUCCESS) continue;

        GpuGraph graph;
        if (gpuGraphCreate(device, &graph) == GPU_SUCCESS) {
            gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_MULTI_QUEUE);
            gpuGraphSetPassProfiling(graph, true);

            GpuGraphResource counterRes = gpuGraphImportBuffer(graph, counterBuffer, GPU_RESOURCE_STATE_UNORDERED_ACCESS, "counter");
            GpuGraphResource backbufferRes = gpuGraphImportSurfaceTexture(graph, backbuffer, "backbuffer");

            GpuGraphPass computePass = gpuGraphAddComputePass(graph, "async_compute_prepass");
            gpuGraphPassWrite(computePass, counterRes);
            gpuGraphPassSetCallback(computePass, compute_prepass_callback, &graphData);

            GpuGraphPass renderPass = gpuGraphAddRenderPass(graph, "triangle");
            GpuGraphColorAttachment ca = {
                .resource = backbufferRes,
                .loadOp = GPU_LOAD_OP_CLEAR,
                .storeOp = GPU_STORE_OP_STORE,
                .clearColor = { 0.05f, 0.08f, 0.12f, 1.0f }
            };
            gpuGraphPassSetColorAttachments(renderPass, 1, &ca);
            gpuGraphPassSetCallback(renderPass, triangle_callback, &graphData);

            if (gpuGraphCompile(graph) == GPU_SUCCESS) {
                if (frameCount == 0) {
                    printf("  requested mode: MULTI_QUEUE, effective: %d\n", (int)gpuGraphGetEffectiveExecuteMode(graph));
                    printf("  passes: compute='%s' (%d), render='%s' (%d)\n",
                           gpuGraphGetPassName(graph, 0), (int)gpuGraphGetPassKind(graph, 0),
                           gpuGraphGetPassName(graph, 1), (int)gpuGraphGetPassKind(graph, 1));
                    uint32_t warnCount = gpuGraphGetValidationWarningCount(graph);
                    for (uint32_t wi = 0; wi < warnCount; wi++)
                        printf("  compile warning: %s\n", gpuGraphGetValidationWarning(graph, wi));
                }
                gpuGraphExecute(graph, queue);
                if (frameCount == 0) {
                    gpuQueueWaitOnHost(queue);
                    uint32_t counterValue = 0;
                    if (gpuDownloadFromBuffer(device, counterBuffer, &counterValue, sizeof(counterValue), 0) == GPU_SUCCESS)
                        printf("  counter after compute: %u (expected 42)\n", counterValue);
                    float computeMs = 0.0f, renderMs = 0.0f;
                    if (gpuGraphGetPassGpuDurationMs(graph, 0, &computeMs) == GPU_SUCCESS)
                        printf("  compute pass GPU: %.4f ms\n", computeMs);
                    if (gpuGraphGetPassGpuDurationMs(graph, 1, &renderMs) == GPU_SUCCESS)
                        printf("  render pass GPU: %.4f ms\n", renderMs);
                }
            }
            gpuGraphDestroy(graph);
        }

        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(backbuffer);
        frameCount++;
    }

    printf("Rendered %u frames\n", frameCount);
    gpuQueueWaitOnHost(queue);
    gpuDestroyComputeBinding(computeBinding);
    gpuDestroyComputePipeline(device, computePipeline);
    gpuDestroyShaderProgram(fillProgram);
    gpuDestroyBuffer(device, counterBuffer);
    gpuDestroyRenderPipeline(device, pipeline);
    gpuDestroyShaderProgram(program);
    gpuDestroyShaderCompiler(compiler);
    gpuSurfaceUnconfigure(surface);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();
    return 0;
}
