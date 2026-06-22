#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    GpuRenderPipeline pipeline;
} TriangleGraphData;

static void triangle_callback(GpuGraphPassContext* ctx, void* userData)
{
    TriangleGraphData* data = (TriangleGraphData*)userData;
    if (ctx->renderPass && data->pipeline) {
        gpuCmdBindRenderPipeline(ctx->renderPass, data->pipeline);
        gpuCmdSetViewport(ctx->renderPass, 0.0f, 0.0f, 800.0f, 600.0f);
        gpuCmdDraw(ctx->renderPass, 3, 1, 0, 0);
    }
}

int main(void)
{
    GpuResult res;
    res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuWindow window = NULL;
    GpuWindowDesc winDesc = {
        .title = "22_triangle_graph",
        .width = 800,
        .height = 600,
        .vsync = true,
        .resizable = true,
    };
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) { printf("Window failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "22_triangle_graph", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuSurface surface = NULL;
    res = gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    if (res != GPU_SUCCESS) { printf("Surface failed: %d\n", res); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuFormat fmt = gpuSurfaceGetPreferredFormat(surface);
    res = gpuSurfaceConfigure(surface, 800, 600, fmt, true);
    if (res != GPU_SUCCESS) { printf("Surface configure failed: %d\n", res); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("Queue failed: %d\n", res); gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuShaderCompiler compiler = NULL;
    res = gpuCreateShaderCompiler(device, &compiler);
    if (res != GPU_SUCCESS) { printf("Compiler failed: %d\n", res); gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuShaderCompileDesc compileDesc = {
        .sourcePath = "triangle.slang",
        .entryPoint = "vertexMain",
        .fragmentEntryPoint = "fragmentMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderProgram program = NULL;
    res = gpuCompileShader(compiler, &compileDesc, &program);
    if (res != GPU_SUCCESS) {
        const char* diag = gpuGetShaderCompileDiagnostic(compiler);
        printf("Shader compile failed: %d %s\n", res, diag ? diag : "");
        gpuDestroyShaderCompiler(compiler);
        gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown();
        return 1;
    }
    printf("Shader compiled successfully\n");

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
    if (res != GPU_SUCCESS) {
        printf("Pipeline creation failed: %d\n", res);
        gpuDestroyShaderProgram(program); gpuDestroyShaderCompiler(compiler);
        gpuSurfaceUnconfigure(surface); gpuDestroySurface(device, surface); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown();
        return 1;
    }
    printf("Pipeline created successfully\n");

    TriangleGraphData graphData = { .pipeline = pipeline };

    printf("Rendering 300 frames with render graph...\n");
    uint32_t frameCount = 0;
    GpuPlatformEvent ev;
    int quit = 0;
    while (frameCount < 300 && !quit) {
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
            GpuGraphResource backbufferRes = gpuGraphImportSurfaceTexture(graph, backbuffer, "backbuffer");

            GpuGraphPass pass = gpuGraphAddRenderPass(graph, "triangle");
            GpuGraphColorAttachment ca = {
                .resource = backbufferRes,
                .loadOp = GPU_LOAD_OP_CLEAR,
                .storeOp = GPU_STORE_OP_STORE,
                .clearColor = { 0.1f, 0.1f, 0.2f, 1.0f }
            };
            gpuGraphPassSetColorAttachments(pass, 1, &ca);
            gpuGraphPassSetCallback(pass, triangle_callback, &graphData);

            if (gpuGraphCompile(graph) == GPU_SUCCESS) {
                gpuGraphExecute(graph, queue);
            }
            gpuGraphDestroy(graph);
        }

        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(backbuffer);
        frameCount++;
    }

    printf("Rendered %u frames\n", frameCount);
    gpuQueueWaitOnHost(queue);
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
