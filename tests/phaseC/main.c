#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
#include <stdlib.h>
#else
#include <unistd.h>
#endif

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

static GpuBackend testBackend(void)
{
    const char* value = getenv("GPU_TEST_BACKEND");
    if (!value || !value[0] || strcmp(value, "default") == 0) return GPU_BACKEND_DEFAULT;
    if (strcmp(value, "d3d12") == 0) return GPU_BACKEND_D3D12;
    if (strcmp(value, "vulkan") == 0) return GPU_BACKEND_VULKAN;
    fprintf(stderr, "FAIL: GPU_TEST_BACKEND must be default, d3d12, or vulkan\n");
    exit(2);
}

static bool isSoftwareVulkanAdapter(GpuDevice device)
{
    GpuCapabilities caps = {};
    gpuGetCapabilities(device, &caps);
    return strstr(caps.adapterName, "llvmpipe") != NULL ||
           strstr(caps.adapterName, "lavapipe") != NULL ||
           strstr(caps.adapterName, "LLVM") != NULL;
}

static void test_callback(GpuGraphPassContext* ctx, void* ud)
{
    (void)ctx;
    int* called = (int*)ud;
    *called = 1;
}

static void noop_pass_callback(GpuGraphPassContext* ctx, void* ud)
{
    (void)ctx;
    (void)ud;
}

typedef struct {
    GpuGraphResource output;
    GpuGraphResource arguments;
    GpuPipelineHandle pipeline;
    GpuResult result;
    GpuResult dataResult;
} GraphBindingTestData;

typedef struct {
    GpuGraphResource source;
    GpuGraphResource target;
    GpuPipelineHandle pipeline;
    GpuResult result;
} TextureBindingTestData;

static void graph_binding_callback(GpuGraphPassContext* ctx, void* userData)
{
    GraphBindingTestData* data = (GraphBindingTestData*)userData;
    GpuBufferHandle output = GPU_NULL_HANDLE;
    data->result = gpuGraphPassGetBuffer(ctx, data->output, &output);
    if (data->result != GPU_SUCCESS) return;
    data->result = gpuCmdBindComputePipelineHandle(ctx->computePass, data->pipeline);
    if (data->result != GPU_SUCCESS) return;
    data->result = gpuCmdSetComputeBindingBuffer(
        ctx->computePass, 0, 0, output, 0, 4 * sizeof(uint32_t),
        GPU_BUFFER_BINDING_READ_WRITE);
    if (data->result != GPU_SUCCESS) return;
    const uint32_t baseValue = 41;
    data->dataResult = gpuCmdSetComputeBindingData(
        ctx->computePass, 0, 1, &baseValue, sizeof(baseValue));
    if (data->dataResult != GPU_SUCCESS) {
        data->result = data->dataResult;
        return;
    }
    if (data->arguments != GPU_GRAPH_NULL_RESOURCE) {
        GpuBufferHandle arguments = GPU_NULL_HANDLE;
        data->result = gpuGraphPassGetBuffer(ctx, data->arguments, &arguments);
        if (data->result != GPU_SUCCESS) return;
        data->result = gpuCmdDispatchComputeIndirect(ctx->computePass, arguments, 0);
    } else {
        gpuCmdDispatchCompute(ctx->computePass, 4, 1, 1);
    }
}

static void texture_binding_callback(GpuGraphPassContext* ctx, void* userData)
{
    TextureBindingTestData* data = (TextureBindingTestData*)userData;
    GpuTextureHandle source = GPU_NULL_HANDLE;
    GpuTextureHandle target = GPU_NULL_HANDLE;
    data->result = gpuGraphPassGetTexture(ctx, data->source, &source);
    if (data->result != GPU_SUCCESS) return;
    data->result = gpuGraphPassGetTexture(ctx, data->target, &target);
    if (data->result != GPU_SUCCESS) return;
    data->result = gpuCmdBindComputePipelineHandle(ctx->computePass, data->pipeline);
    if (data->result != GPU_SUCCESS) return;
    data->result = gpuCmdSetComputeBindingTexture(ctx->computePass, 0, 0, source);
    if (data->result != GPU_SUCCESS) return;
    data->result = gpuCmdSetComputeBindingTexture(ctx->computePass, 0, 1, target);
    if (data->result != GPU_SUCCESS) return;
    const uint32_t parameters[4] = {4, 4, 2, 2};
    data->result = gpuCmdSetComputeBindingData(
        ctx->computePass, 0, 2, parameters, sizeof(parameters));
    if (data->result != GPU_SUCCESS) return;
    gpuCmdDispatchCompute(ctx->computePass, 1, 1, 1);
}

int main(void)
{
    printf("=== Phase C: Render Graph Test ===\n\n"); flush();

    GpuDevice device;
    const GpuBackend backend = testBackend();
    GpuDeviceDesc devDesc = {
        .appName = "phaseC_test", .enableDebugLayer = true, .preferredBackend = backend,
    };
    CHECK(gpuCreateDevice(&devDesc, &device));

    GpuCommandQueue queue;
    CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

    /* C.1 Graph Create / Destroy */
    printf("[C.1] Graph Create/Destroy\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        CHECK_TRUE(gpuGraphGetPassCount(graph) == 0);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.2 Graph Reset */
    printf("[C.2] Graph Reset\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "gbuffer"
        };
        gpuGraphCreateTexture(graph, &tdesc, "gbuffer");
        gpuGraphAddRenderPass(graph, "pass0");
        CHECK_TRUE(gpuGraphGetPassCount(graph) == 1);
        gpuGraphReset(graph);
        CHECK_TRUE(gpuGraphGetPassCount(graph) == 0);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.3 Logical Texture Resource */
    printf("[C.3] Logical Texture Resource\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 128, .height = 128,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "color"
        };
        GpuGraphResource color = gpuGraphCreateTexture(graph, &tdesc, "color");
        CHECK_TRUE(color != GPU_GRAPH_NULL_RESOURCE);
        GpuTextureHandle h = gpuGraphGetTexture(graph, color);
        CHECK_TRUE(h.index == 0);
        CHECK_TRUE(h.generation == 0);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.4 Logical Buffer Resource */
    printf("[C.4] Logical Buffer Resource\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 1024, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_UNORDERED_ACCESS,
            .label = "cbuffer"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "cbuffer");
        CHECK_TRUE(buf != GPU_GRAPH_NULL_RESOURCE);
        GpuBufferHandle h = gpuGraphGetBuffer(graph, buf);
        CHECK_TRUE(h.index == 0);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.5 Import Texture */
    printf("[C.5] Import Texture\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "imported"
        };
        GpuTextureHandle tex;
        CHECK(gpuCreateTexture(device, &tdesc, &tex));
        GpuGraphResource imported = gpuGraphImportTexture(graph, tex, GPU_RESOURCE_STATE_SHADER_RESOURCE, "imported");
        CHECK_TRUE(imported != GPU_GRAPH_NULL_RESOURCE);
        GpuTextureHandle h = gpuGraphGetTexture(graph, imported);
        CHECK_TRUE(h.index == tex.index);
        CHECK_TRUE(h.generation == tex.generation);
        gpuGraphDestroy(graph);
        gpuDestroyTexture(device, tex);
    }
    printf("  OK\n"); flush();

    /* C.6 Import Buffer */
    printf("[C.6] Import Buffer\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "imported_buf"
        };
        GpuBufferHandle buf;
        CHECK(gpuCreateBuffer(device, &bdesc, &buf));
        GpuGraphResource imported = gpuGraphImportBuffer(graph, buf, GPU_RESOURCE_STATE_SHADER_RESOURCE, "imported_buf");
        CHECK_TRUE(imported != GPU_GRAPH_NULL_RESOURCE);
        GpuBufferHandle h = gpuGraphGetBuffer(graph, imported);
        CHECK_TRUE(h.index == buf.index);
        CHECK_TRUE(h.generation == buf.generation);
        gpuGraphDestroy(graph);
        gpuDestroyBuffer(device, buf);
    }
    printf("  OK\n"); flush();

    /* C.7 Render Pass with Color Attachment */
    printf("[C.7] Render Pass with Color Attachment\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "target"
        };
        GpuGraphResource target = gpuGraphCreateTexture(graph, &tdesc, "target");
        GpuGraphPass pass = gpuGraphAddRenderPass(graph, "clear_pass");
        GpuGraphColorAttachment ca = {
            .resource = target,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(pass, 1, &ca);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetPassCount(graph) == 1);
        GpuTextureHandle realized = gpuGraphGetTexture(graph, target);
        CHECK_TRUE(realized.index != 0);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.8 Compute Pass */
    printf("[C.8] Compute Pass\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "uav_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "uav_buf");
        GpuGraphPass pass = gpuGraphAddComputePass(graph, "compute_pass");
        gpuGraphPassWrite(pass, buf);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetPassCount(graph) == 1);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.9 Copy Pass */
    printf("[C.9] Copy Pass\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc srcDesc = {
            .size = 128, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_COPY_SOURCE,
            .label = "copy_src"
        };
        GpuBufferDesc dstDesc = {
            .size = 128, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_COPY_DEST,
            .label = "copy_dst"
        };
        GpuGraphResource src = gpuGraphCreateBuffer(graph, &srcDesc, "copy_src");
        GpuGraphResource dst = gpuGraphCreateBuffer(graph, &dstDesc, "copy_dst");
        GpuGraphPass pass = gpuGraphAddCopyPass(graph, "copy_pass");
        gpuGraphPassRead(pass, src);
        gpuGraphPassWrite(pass, dst);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.40 Copy Pass data verification */
    printf("[C.40] Copy Pass data verification\n"); flush();
    {
        uint32_t srcData[32];
        for (int i = 0; i < 32; i++) srcData[i] = (uint32_t)i;

        GpuBufferDesc srcDesc = {
            .size = sizeof(srcData), .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_COPY_SOURCE,
            .label = "copy_src_init"
        };
        GpuBufferDesc dstDesc = {
            .size = sizeof(srcData), .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_COPY_DEST | GPU_BUFFER_USAGE_COPY_SOURCE,
            .label = "copy_dst_verify"
        };
        GpuBufferHandle srcHandle, dstHandle;
        CHECK(gpuCreateBufferInit(device, &srcDesc, srcData, &srcHandle));
        CHECK(gpuCreateBuffer(device, &dstDesc, &dstHandle));

        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuGraphResource src = gpuGraphImportBuffer(graph, srcHandle, GPU_RESOURCE_STATE_COPY_SOURCE, "copy_src");
        GpuGraphResource dst = gpuGraphImportBuffer(graph, dstHandle, GPU_RESOURCE_STATE_COPY_DEST, "copy_dst");
        GpuGraphPass pass = gpuGraphAddCopyPass(graph, "copy_verify");
        gpuGraphPassRead(pass, src);
        gpuGraphPassWrite(pass, dst);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);

        uint32_t readback[32] = {0};
        CHECK(gpuDownloadFromBuffer(device, dstHandle, readback, sizeof(readback), 0));
        for (int i = 0; i < 32; i++)
            CHECK_TRUE(readback[i] == (uint32_t)i);

        gpuGraphDestroy(graph);
        gpuDestroyBuffer(device, dstHandle);
        gpuDestroyBuffer(device, srcHandle);
    }
    printf("  OK\n"); flush();

    /* C.10 Multi-Pass Dependency */
    printf("[C.10] Multi-Pass Dependency\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "intermediate"
        };
        GpuGraphResource intermediate = gpuGraphCreateTexture(graph, &tdesc, "intermediate");

        GpuGraphPass pass0 = gpuGraphAddRenderPass(graph, "gbuffer");
        GpuGraphColorAttachment ca0 = {
            .resource = intermediate,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 1.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(pass0, 1, &ca0);

        GpuGraphPass pass1 = gpuGraphAddRenderPass(graph, "lighting");
        gpuGraphPassRead(pass1, intermediate);

        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetPassCount(graph) == 2);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.11 Pass Culling - write-only pass with no consumers should be retained (has callback) */
    printf("[C.11] Pass Culling\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 32, .height = 32,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET,
            .label = "orphan"
        };
        GpuGraphResource orphan = gpuGraphCreateTexture(graph, &tdesc, "orphan");
        GpuGraphPass pass = gpuGraphAddRenderPass(graph, "orphan_pass");
        GpuGraphColorAttachment ca = {
            .resource = orphan,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(pass, 1, &ca);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetPassCount(graph) == 1);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.12 Render Pass with Depth Attachment */
    printf("[C.12] Render Pass with Depth Attachment\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));

        GpuTextureDesc colorDesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "color"
        };
        GpuTextureDesc depthDesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_D32_FLOAT, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_DEPTH_STENCIL,
            .label = "depth"
        };
        GpuGraphResource colorRes = gpuGraphCreateTexture(graph, &colorDesc, "color");
        GpuGraphResource depthRes = gpuGraphCreateTexture(graph, &depthDesc, "depth");

        GpuGraphPass pass = gpuGraphAddRenderPass(graph, "depth_pass");

        GpuGraphColorAttachment ca = {
            .resource = colorRes,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(pass, 1, &ca);

        GpuGraphDepthAttachment da = {
            .resource = depthRes,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_DONT_CARE,
            .clearDepth = 1.0f,
            .clearStencil = 0
        };
        gpuGraphPassSetDepthAttachment(pass, &da);

        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.13 Pass Callback Execution */
    printf("[C.13] Pass Callback Execution\n"); flush();
    {
        static int cbCalled = 0;
        cbCalled = 0;
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 64, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS,
            .label = "cb_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "cb_buf");
        GpuGraphPass pass = gpuGraphAddComputePass(graph, "cb_pass");
        gpuGraphPassWrite(pass, buf);

        gpuGraphPassSetCallback(pass, test_callback, &cbCalled);

        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        CHECK_TRUE(cbCalled == 1);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.14 Pass Kind / Name Query */
    printf("[C.14] Pass Kind/Name Query\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 64, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS,
            .label = "qbuf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "qbuf");
        gpuGraphPassWrite(gpuGraphAddComputePass(graph, "my_compute"), buf);
        gpuGraphPassRead(gpuGraphAddCopyPass(graph, "my_copy"), buf);

        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetPassKind(graph, 0) == GPU_GRAPH_PASS_COMPUTE);
        CHECK_TRUE(strcmp(gpuGraphGetPassName(graph, 0), "my_compute") == 0);
        CHECK_TRUE(gpuGraphGetPassKind(graph, 1) == GPU_GRAPH_PASS_COPY);
        CHECK_TRUE(strcmp(gpuGraphGetPassName(graph, 1), "my_copy") == 0);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.15 Export DOT/JSON */
    printf("[C.15] Export DOT/JSON\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 32, .height = 32,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "exp_tex"
        };
        GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "exp_tex");
        GpuGraphPass p0 = gpuGraphAddRenderPass(graph, "pass_a");
        GpuGraphColorAttachment ca = {
            .resource = tex,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(p0, 1, &ca);
        GpuGraphPass p1 = gpuGraphAddRenderPass(graph, "pass_b");
        gpuGraphPassRead(p1, tex);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExportDot(graph, "phaseC_test.dot"));
        CHECK(gpuGraphExportJson(graph, "phaseC_test.json"));
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.16 Stress: 100 compile/execute cycles with transient resources */
    printf("[C.16] Stress: 100 cycles\n"); flush();
    {
        for (int i = 0; i < 100; i++) {
            GpuGraph graph;
            CHECK(gpuGraphCreate(device, &graph));
            GpuTextureDesc tdesc = {
                .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
                .depth = 1, .arrayLength = 1, .mipCount = 1,
                .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
                .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
                .label = "stress"
            };
            GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "stress");
            GpuGraphPass p = gpuGraphAddRenderPass(graph, "s_pass");
            GpuGraphColorAttachment ca = {
                .resource = tex,
                .loadOp = GPU_LOAD_OP_CLEAR,
                .storeOp = GPU_STORE_OP_STORE,
                .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
            };
            gpuGraphPassSetColorAttachments(p, 1, &ca);
            CHECK(gpuGraphCompile(graph));
            CHECK(gpuGraphExecute(graph, queue));
            gpuGraphDestroy(graph);
        }
        gpuQueueWaitOnHost(queue);
    }
    printf("  OK\n"); flush();

    /* C.17 Read-Read (no barrier needed) */
    printf("[C.17] Read-Read no barrier\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "rr_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "rr_buf");
        GpuGraphPass p0 = gpuGraphAddComputePass(graph, "reader0");
        gpuGraphPassRead(p0, buf);
        GpuGraphPass p1 = gpuGraphAddComputePass(graph, "reader1");
        gpuGraphPassRead(p1, buf);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.18 Invalid args */
    printf("[C.18] Invalid args\n"); flush();
    {
        CHECK_TRUE(gpuGraphCreate(NULL, NULL) == GPU_ERROR_INVALID_ARGS);
        CHECK_TRUE(gpuGraphExecute(NULL, NULL) == GPU_ERROR_INVALID_ARGS);
        CHECK_TRUE(gpuGraphCompile(NULL) == GPU_ERROR_INVALID_ARGS);
        CHECK_TRUE(gpuGraphGetTexture(NULL, GPU_GRAPH_NULL_RESOURCE).index == 0);
        CHECK_TRUE(gpuGraphGetBuffer(NULL, GPU_GRAPH_NULL_RESOURCE).index == 0);
        CHECK_TRUE(gpuGraphGetPassCount(NULL) == 0);
    }
    printf("  OK\n"); flush();

    /* C.19 Queue alias info */
    printf("[C.19] Queue alias info\n"); flush();
    {
        GpuQueueInfo graphicsInfo = {};
        GpuQueueInfo computeInfo = {};
        GpuQueueInfo transferInfo = {};
        CHECK(gpuGetQueueInfo(device, GPU_QUEUE_TYPE_GRAPHICS, &graphicsInfo));
        CHECK(gpuGetQueueInfo(device, GPU_QUEUE_TYPE_COMPUTE, &computeInfo));
        CHECK(gpuGetQueueInfo(device, GPU_QUEUE_TYPE_TRANSFER, &transferInfo));
        CHECK_TRUE(graphicsInfo.support == GPU_QUEUE_SUPPORT_DEDICATED);
        if (gpuDeviceSupportsIndependentQueues(device)) {
            CHECK_TRUE(computeInfo.support == GPU_QUEUE_SUPPORT_DEDICATED);
            CHECK_TRUE(transferInfo.support == GPU_QUEUE_SUPPORT_DEDICATED);
        } else {
            CHECK_TRUE(computeInfo.support == GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS);
            CHECK_TRUE(transferInfo.support == GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS);
            CHECK_TRUE(computeInfo.reason != NULL && computeInfo.reason[0] != '\0');
        }
    }
    printf("  OK\n"); flush();

    /* C.20 Execute modes */
    printf("[C.20] Execute modes\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        CHECK_TRUE(gpuGraphGetExecuteMode(graph) == GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER);
        gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_PASS_SUBMIT);
        CHECK_TRUE(gpuGraphGetExecuteMode(graph) == GPU_GRAPH_EXECUTE_PASS_SUBMIT);

        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "mode_tex"
        };
        GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "mode_tex");
        GpuGraphPass p0 = gpuGraphAddRenderPass(graph, "mode_pass0");
        GpuGraphColorAttachment ca0 = {
            .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(p0, 1, &ca0);
        GpuGraphPass p1 = gpuGraphAddRenderPass(graph, "mode_pass1");
        gpuGraphPassRead(p1, tex);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);

        gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER);
        gpuGraphReset(graph);
        tex = gpuGraphCreateTexture(graph, &tdesc, "mode_tex2");
        p0 = gpuGraphAddRenderPass(graph, "mode_pass0");
        ca0.resource = tex;
        gpuGraphPassSetColorAttachments(p0, 1, &ca0);
        p1 = gpuGraphAddRenderPass(graph, "mode_pass1");
        gpuGraphPassRead(p1, tex);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.21 JSON/DOT string export */
    printf("[C.21] JSON/DOT string export\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 32, .height = 32,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "exp_tex"
        };
        GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "exp_tex");
        GpuGraphPass p0 = gpuGraphAddRenderPass(graph, "pass_a");
        GpuGraphColorAttachment ca = {
            .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(p0, 1, &ca);
        GpuGraphPass p1 = gpuGraphAddRenderPass(graph, "pass_b");
        gpuGraphPassRead(p1, tex);
        CHECK(gpuGraphCompile(graph));

        size_t jsonSize = 0;
        CHECK_TRUE(gpuGraphExportJsonString(graph, NULL, &jsonSize) == GPU_ERROR_BUFFER_TOO_SMALL);
        CHECK_TRUE(jsonSize > 32);
        char* jsonBuf = (char*)malloc(jsonSize);
        CHECK_TRUE(jsonBuf != NULL);
        CHECK(gpuGraphExportJsonString(graph, jsonBuf, &jsonSize));
        CHECK_TRUE(strstr(jsonBuf, "\"barriers\"") != NULL);
        CHECK_TRUE(strstr(jsonBuf, "\"first_use_pass\"") != NULL);
        free(jsonBuf);

        size_t dotSize = 0;
        CHECK_TRUE(gpuGraphExportDotString(graph, NULL, &dotSize) == GPU_ERROR_BUFFER_TOO_SMALL);
        char* dotBuf = (char*)malloc(dotSize);
        CHECK_TRUE(dotBuf != NULL);
        CHECK(gpuGraphExportDotString(graph, dotBuf, &dotSize));
        CHECK_TRUE(strstr(dotBuf, "res0") != NULL);
        free(dotBuf);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.22 Timestamp encoder API */
    printf("[C.22] Timestamp encoder API\n"); flush();
    {
        GpuQueryPool pool;
        CHECK(gpuCreateQueryPool(device, 2, &pool));
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
        CHECK_TRUE(encoder != NULL);
        gpuCmdWriteTimestampEncoder(encoder, pool, 0);
        GpuComputePassEncoder cp = gpuCmdBeginComputePass(encoder);
        CHECK_TRUE(cp != NULL);
        gpuCmdWriteComputeTimestamp(cp, pool, 1);
        gpuCmdEndComputePass(cp);
        GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(cmd != NULL);
        CHECK(gpuQueueSubmit(queue, 1, &cmd));
        gpuQueueWaitOnHost(queue);
        gpuDestroyQueryPool(device, pool);
    }
    printf("  OK\n"); flush();

    /* C.23 Transient texture pool reuse */
    printf("[C.23] Transient texture pool\n"); flush();
    {
        uint32_t poolBefore = gpuGetTransientTexturePoolCount(device);
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "pool_tex"
        };
        for (int i = 0; i < 100; i++) {
            GpuGraph graph;
            CHECK(gpuGraphCreate(device, &graph));
            GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "pool_tex");
            GpuGraphPass p = gpuGraphAddRenderPass(graph, "pool_pass");
            GpuGraphColorAttachment ca = {
                .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
                .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
            };
            gpuGraphPassSetColorAttachments(p, 1, &ca);
            CHECK(gpuGraphCompile(graph));
            CHECK(gpuGraphExecute(graph, queue));
            gpuGraphDestroy(graph);
        }
        gpuQueueWaitOnHost(queue);
        uint32_t poolAfter = gpuGetTransientTexturePoolCount(device);
        CHECK_TRUE(poolAfter == poolBefore + 1);
        printf("  pool textures: before=%u after=%u\n", poolBefore, poolAfter);
    }
    printf("  OK\n"); flush();

    /* C.24 Barrier chain CopyDest -> ShaderResource -> RenderTarget */
    printf("[C.24] Barrier state chain\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_COPY_DEST | GPU_TEXTURE_USAGE_SHADER_RESOURCE |
                     GPU_TEXTURE_USAGE_RENDER_TARGET,
            .label = "chain_tex"
        };
        GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "chain_tex");
        GpuGraphPass copyPass = gpuGraphAddCopyPass(graph, "upload");
        gpuGraphPassWrite(copyPass, tex);
        GpuGraphPass readPass = gpuGraphAddComputePass(graph, "sample");
        gpuGraphPassRead(readPass, tex);
        GpuGraphPass drawPass = gpuGraphAddRenderPass(graph, "draw");
        GpuGraphColorAttachment ca = {
            .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.2f, 0.3f, 0.4f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(drawPass, 1, &ca);
        CHECK(gpuGraphCompile(graph));

        GpuGraphBarrierInfo info;
        CHECK_TRUE(gpuGraphGetPassBarrierCount(graph, 2) > 0);
        CHECK(gpuGraphGetPassBarrier(graph, 2, 0, &info));
        CHECK_TRUE(info.after == GPU_RESOURCE_STATE_RENDER_TARGET);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.25 UAV global barrier between compute passes */
    printf("[C.25] UAV global barrier\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "uav_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "uav_buf");
        GpuGraphPass wpass = gpuGraphAddComputePass(graph, "uav_write");
        gpuGraphPassWrite(wpass, buf);
        GpuGraphPass rpass = gpuGraphAddComputePass(graph, "uav_read");
        gpuGraphPassRead(rpass, buf);
        CHECK(gpuGraphCompile(graph));

        bool foundGlobal = false;
        uint32_t readPassIndex = gpuGraphGetExecutionOrderPassIndex(graph, 1);
        for (uint32_t bi = 0; bi < gpuGraphGetPassBarrierCount(graph, readPassIndex); bi++) {
            GpuGraphBarrierInfo info;
            CHECK(gpuGraphGetPassBarrier(graph, readPassIndex, bi, &info));
            if (info.isGlobalBarrier) foundGlobal = true;
        }
        CHECK_TRUE(foundGlobal);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.26 Depth write -> depth read transition */
    printf("[C.26] Depth barrier transition\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc depthDesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_D32_FLOAT, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_DEPTH_STENCIL | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "depth_tex"
        };
        GpuGraphResource depth = gpuGraphCreateTexture(graph, &depthDesc, "depth_tex");
        GpuGraphPass zpass = gpuGraphAddRenderPass(graph, "z_prepass");
        GpuGraphDepthAttachment da = {
            .resource = depth, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearDepth = 1.0f, .clearStencil = 0
        };
        gpuGraphPassSetDepthAttachment(zpass, &da);
        GpuGraphPass spass = gpuGraphAddComputePass(graph, "depth_sample");
        gpuGraphPassRead(spass, depth);
        CHECK(gpuGraphCompile(graph));

        uint32_t samplePass = gpuGraphGetExecutionOrderPassIndex(graph, 1);
        GpuGraphBarrierInfo info;
        CHECK_TRUE(gpuGraphGetPassBarrierCount(graph, samplePass) > 0);
        CHECK(gpuGraphGetPassBarrier(graph, samplePass, 0, &info));
        CHECK_TRUE(info.after == GPU_RESOURCE_STATE_DEPTH_READ);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.27 Lifetime-based transient aliasing */
    printf("[C.27] Lifetime transient aliasing\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "alias_tex"
        };
        GpuGraphResource texA = gpuGraphCreateTexture(graph, &tdesc, "tex_a");
        GpuGraphResource texB = gpuGraphCreateTexture(graph, &tdesc, "tex_b");

        GpuGraphPass p0 = gpuGraphAddRenderPass(graph, "pass_a");
        GpuGraphColorAttachment ca0 = {
            .resource = texA, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 1.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(p0, 1, &ca0);

        GpuGraphPass p1 = gpuGraphAddRenderPass(graph, "pass_b");
        GpuGraphColorAttachment ca1 = {
            .resource = texB, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 1.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(p1, 1, &ca1);

        CHECK(gpuGraphCompile(graph));

        GpuGraphResourceLifetimeInfo lifeA, lifeB;
        CHECK(gpuGraphGetResourceLifetime(graph, texA, &lifeA));
        CHECK(gpuGraphGetResourceLifetime(graph, texB, &lifeB));
        CHECK_TRUE(lifeA.allocationId == lifeB.allocationId);
        CHECK_TRUE(lifeB.aliased);
        CHECK_TRUE(gpuGraphGetTexture(graph, texA).index == gpuGraphGetTexture(graph, texB).index);

        size_t jsonSize = 0;
        CHECK_TRUE(gpuGraphExportJsonString(graph, NULL, &jsonSize) == GPU_ERROR_BUFFER_TOO_SMALL);
        char* jsonBuf = (char*)malloc(jsonSize);
        CHECK_TRUE(jsonBuf != NULL);
        CHECK(gpuGraphExportJsonString(graph, jsonBuf, &jsonSize));
        CHECK_TRUE(strstr(jsonBuf, "\"allocation_id\"") != NULL);
        CHECK_TRUE(strstr(jsonBuf, "\"aliased\": true") != NULL);
        free(jsonBuf);

        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.28 1000-frame transient pool bound */
    printf("[C.28] 1000-frame pool bound\n"); flush();
    {
        uint32_t poolBefore = gpuGetTransientTexturePoolCount(device);
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "bound_tex"
        };
        for (int i = 0; i < 1000; i++) {
            GpuGraph graph;
            CHECK(gpuGraphCreate(device, &graph));
            GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "bound_tex");
            GpuGraphPass p = gpuGraphAddRenderPass(graph, "bound_pass");
            GpuGraphColorAttachment ca = {
                .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
                .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
            };
            gpuGraphPassSetColorAttachments(p, 1, &ca);
            CHECK(gpuGraphCompile(graph));
            CHECK(gpuGraphExecute(graph, queue));
            gpuGraphDestroy(graph);
        }
        gpuQueueWaitOnHost(queue);
        uint32_t poolAfter = gpuGetTransientTexturePoolCount(device);
        CHECK_TRUE(poolAfter <= poolBefore + 1);
        printf("  pool textures: before=%u after=%u\n", poolBefore, poolAfter);
    }
    printf("  OK\n"); flush();

    /* C.29 Multi-queue mode downgrade on alias queue */
    printf("[C.29] Multi-queue execute mode\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_MULTI_QUEUE);
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 32, .height = 32,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "mq_tex"
        };
        GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "mq_tex");
        GpuGraphPass rp = gpuGraphAddRenderPass(graph, "render_pass");
        GpuGraphColorAttachment ca = {
            .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(rp, 1, &ca);
        GpuGraphPass cp = gpuGraphAddComputePass(graph, "compute_pass");
        gpuGraphPassRead(cp, tex);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetExecuteMode(graph) == GPU_GRAPH_EXECUTE_MULTI_QUEUE);
        if (gpuDeviceSupportsIndependentQueues(device)) {
            CHECK_TRUE(gpuGraphGetEffectiveExecuteMode(graph) == GPU_GRAPH_EXECUTE_MULTI_QUEUE);
        } else {
            CHECK_TRUE(gpuGraphGetEffectiveExecuteMode(graph) == GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER);
            bool foundDowngrade = false;
            for (uint32_t wi = 0; wi < gpuGraphGetValidationWarningCount(graph); wi++) {
                if (strstr(gpuGraphGetValidationWarning(graph, wi), "downgraded") != NULL)
                    foundDowngrade = true;
            }
            CHECK_TRUE(foundDowngrade);
        }

        if (!isSoftwareVulkanAdapter(device)) {
            CHECK(gpuGraphExecute(graph, queue));
            gpuQueueWaitOnHost(queue);
        } else {
            printf("  (skipped graph execute on software Vulkan)\n"); flush();
        }
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.46 Graph-owned surface capture */
    printf("[C.46] Graph-owned surface capture\n"); flush();
    {
        CHECK(gpuPlatformInit());
        GpuWindow window;
        GpuWindowDesc windowDesc = {
            .title = "phaseC_surface_capture",
            .width = 32,
            .height = 32,
            .resizable = false,
            .vsync = false,
        };
        CHECK(gpuCreateWindow(&windowDesc, &window));
        GpuDevice captureDevice;
        GpuDeviceDesc deviceDesc = {
            .appName = "phaseC_surface_capture",
            .enableDebugLayer = true,
            .preferredBackend = backend,
        };
        CHECK(gpuCreateDevice(&deviceDesc, &captureDevice));
        GpuSurface surface;
        CHECK(gpuCreateSurface(
            captureDevice, window, GPU_SURFACE_TYPE_D3D12, &surface));
        GpuFormat surfaceFormat = gpuSurfaceGetPreferredFormat(surface);
        CHECK_TRUE(surfaceFormat == GPU_FORMAT_RGBA8_UNORM ||
                   surfaceFormat == GPU_FORMAT_RGBA8_UNORM_SRGB ||
                   surfaceFormat == GPU_FORMAT_BGRA8_UNORM ||
                   surfaceFormat == GPU_FORMAT_BGRA8_UNORM_SRGB);
        CHECK(gpuSurfaceConfigure(surface, 32, 32, surfaceFormat, false));
        GpuCommandQueue captureQueue;
        CHECK(gpuGetQueue(captureDevice, GPU_QUEUE_TYPE_GRAPHICS, &captureQueue));
        GpuSurfaceTexture surfaceTexture;
        CHECK(gpuSurfaceAcquireNextImage(surface, &surfaceTexture));
        GpuTextureFootprint footprint = {0};
        CHECK(gpuGetSurfaceTextureReadbackFootprint(
            surfaceTexture, 0, &footprint));
        CHECK_TRUE(footprint.width == 32 && footprint.height == 32);
        GpuBufferHandle readback;
        CHECK(gpuCreateReadbackBuffer(
            captureDevice, footprint.totalSize, &readback));

        GpuGraph graph;
        CHECK(gpuGraphCreate(captureDevice, &graph));
        GpuGraphResource surfaceResource = gpuGraphImportSurfaceTexture(
            graph, surfaceTexture, "surface_capture_source");
        GpuGraphResource readbackResource = gpuGraphImportBufferEx(
            graph,
            readback,
            GPU_RESOURCE_STATE_COPY_DEST,
            GPU_RESOURCE_STATE_COPY_DEST,
            "surface_capture_readback");
        GpuGraphPass render = gpuGraphAddRenderPass(graph, "surface_clear");
        GpuGraphColorAttachment attachment = {
            .resource = surfaceResource,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearColor = {0.0f, 1.0f, 0.0f, 1.0f},
        };
        gpuGraphPassSetColorAttachments(render, 1, &attachment);
        GpuGraphPass copy = gpuGraphAddCopyPass(graph, "surface_capture");
        CHECK(gpuGraphPassCopyTextureToBuffer(
            copy, surfaceResource, 0, 0, readbackResource, 0));
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetValidationWarningCount(graph) == 0);
        CHECK(gpuGraphExecute(graph, captureQueue));
        CHECK(gpuSurfacePresent(surface));
        CHECK(gpuQueueWaitOnHost(captureQueue));
        CHECK_TRUE(gpuGetDeviceLastDiagnostic(captureDevice)[0] == '\0');

        void* mapped = NULL;
        CHECK(gpuMapReadbackBuffer(captureDevice, readback, &mapped));
        for (uint32_t y = 0; y < footprint.height; ++y) {
            const uint8_t* row =
                (const uint8_t*)mapped + y * footprint.rowPitch;
            for (uint32_t x = 0; x < footprint.width; ++x) {
                const uint8_t* pixel = row + x * 4;
                CHECK_TRUE(pixel[0] == 0u);
                CHECK_TRUE(pixel[1] == 255u);
                CHECK_TRUE(pixel[2] == 0u);
                CHECK_TRUE(pixel[3] == 255u);
            }
        }
        gpuUnmapReadbackBuffer(captureDevice, readback);
        gpuGraphDestroy(graph);
        gpuDestroyBuffer(captureDevice, readback);
        gpuSurfaceTextureRelease(surfaceTexture);
        gpuSurfaceUnconfigure(surface);
        gpuDestroySurface(captureDevice, surface);
        gpuDestroyDevice(captureDevice);
        gpuDestroyWindow(window);
        gpuPlatformShutdown();
    }
    printf("  OK\n"); flush();

    /* C.49 Compute pipeline binary descriptor honors its entry point */
    printf("[C.49] Compute pipeline descriptor entry point\n"); flush();
    {
        static const char computeSource[] =
            "RWStructuredBuffer<uint> gOutput : register(u0);\n"
            "[numthreads(1, 1, 1)]\n"
            "void computeMain(uint3 id : SV_DispatchThreadID) "
            "{ gOutput[id.x] = id.x + 41; }\n";
        GpuComputePipelineDesc2 desc = {
            .label = "compute_descriptor_entry",
            .computeShader = {
                .data = (const uint8_t *)computeSource,
                .size = sizeof(computeSource) - 1,
                .entryPoint = "computeMain",
            },
        };
        GpuPipelineHandle pipeline = GPU_NULL_HANDLE;
        CHECK(gpuCreateComputePipeline2(device, &desc, &pipeline));
        CHECK_TRUE(gpuGetPipelineType(device, pipeline) == GPU_PIPELINE_TYPE_COMPUTE);
        CHECK(gpuDestroyPipeline(device, pipeline));
    }
    printf("  OK\n"); flush();

    /* C.48 Ordered WAW is barrier metadata, not a validation warning */
    printf("[C.48] Ordered write-after-write validation\n"); flush();
    {
        GpuBufferDesc bdesc = {
            .size = 64, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS,
            .label = "ordered_waw_buf"
        };
        GpuBufferHandle handle;
        CHECK(gpuCreateBuffer(device, &bdesc, &handle));
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuGraphResource buffer = gpuGraphImportBufferEx(
            graph,
            handle,
            GPU_RESOURCE_STATE_UNORDERED_ACCESS,
            GPU_RESOURCE_STATE_UNORDERED_ACCESS,
            "ordered_waw_buf");
        GpuGraphPass first = gpuGraphAddComputePass(graph, "ordered_waw_first");
        GpuGraphPass second = gpuGraphAddComputePass(graph, "ordered_waw_second");
        gpuGraphPassWrite(first, buffer);
        gpuGraphPassWrite(second, buffer);
        gpuGraphPassSetCallback(first, noop_pass_callback, NULL);
        gpuGraphPassSetCallback(second, noop_pass_callback, NULL);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetValidationWarningCount(graph) == 0);
        bool foundWawBarrier = false;
        for (uint32_t bi = 0; bi < gpuGraphGetPassBarrierCount(graph, 1); ++bi) {
            GpuGraphBarrierInfo info = {0};
            CHECK(gpuGraphGetPassBarrier(graph, 1, bi, &info));
            if (info.isGlobalBarrier && info.hazardKind == GPU_HAZARD_WRITE_AFTER_WRITE)
                foundWawBarrier = true;
        }
        CHECK_TRUE(foundWawBarrier);
        CHECK(gpuGraphExecute(graph, queue));
        CHECK(gpuQueueWaitOnHost(queue));
        gpuGraphDestroy(graph);
        gpuDestroyBuffer(device, handle);
    }
    printf("  OK\n"); flush();

    /* C.50 Transient integer attachment clear infers its internal UAV usage */
    printf("[C.50] Transient typed uint attachment clear\n"); flush();
    {
        GpuDevice clearDevice;
        GpuDeviceDesc deviceDesc = {
            .appName = "phaseC_transient_typed_clear",
            .enableDebugLayer = true,
            .preferredBackend = backend,
        };
        CHECK(gpuCreateDevice(&deviceDesc, &clearDevice));
        GpuCommandQueue clearQueue;
        CHECK(gpuGetQueue(clearDevice, GPU_QUEUE_TYPE_GRAPHICS, &clearQueue));
        GpuTextureDesc textureDesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = 7,
            .height = 5,
            .depth = 1,
            .arrayLength = 1,
            .mipCount = 1,
            .format = GPU_FORMAT_R32_UINT,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET |
                     GPU_TEXTURE_USAGE_COPY_SOURCE,
            .label = "transient_typed_uint_clear",
        };
        GpuGraph graph;
        CHECK(gpuGraphCreate(clearDevice, &graph));
        GpuGraphResource resource = gpuGraphCreateTexture(
            graph, &textureDesc, "transient_typed_uint_clear");
        GpuGraphPass pass = gpuGraphAddRenderPass(
            graph, "transient_typed_uint_clear");
        GpuGraphColorAttachment attachment = {
            .resource = resource,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearType = GPU_GRAPH_CLEAR_UINT,
            .clearUint = {0xffffffffu, 0u, 0u, 0u},
        };
        gpuGraphPassSetColorAttachments(pass, 1, &attachment);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetValidationWarningCount(graph) == 0);
        CHECK(gpuGraphExecute(graph, clearQueue));
        CHECK(gpuQueueWaitOnHost(clearQueue));
        GpuTextureHandle texture = gpuGraphGetTexture(graph, resource);
        CHECK_TRUE(texture.index != 0);

        GpuTextureFootprint footprint = {0};
        CHECK(gpuGetTextureReadbackFootprint(clearDevice, texture, 0, &footprint));
        GpuBufferHandle readback;
        CHECK(gpuCreateReadbackBuffer(clearDevice, footprint.totalSize, &readback));
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(clearDevice, clearQueue);
        CHECK_TRUE(encoder != NULL);
        CHECK(gpuCmdCopyTextureToBuffer(encoder, texture, 0, 0, readback, 0));
        GpuCommandBuffer commands = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(commands != NULL);
        CHECK(gpuQueueSubmit(clearQueue, 1, &commands));
        CHECK(gpuQueueWaitOnHost(clearQueue));
        void* mapped = NULL;
        CHECK(gpuMapReadbackBuffer(clearDevice, readback, &mapped));
        for (uint32_t y = 0; y < textureDesc.height; ++y) {
            const uint32_t* row =
                (const uint32_t*)((const uint8_t*)mapped + y * footprint.rowPitch);
            for (uint32_t x = 0; x < textureDesc.width; ++x) {
                CHECK_TRUE(row[x] == 0xffffffffu);
            }
        }
        gpuUnmapReadbackBuffer(clearDevice, readback);
        gpuDestroyBuffer(clearDevice, readback);
        gpuGraphDestroy(graph);
        gpuDestroyDevice(clearDevice);
    }
    printf("  OK\n"); flush();

    /* C.51 Graph callback compute texture and constant-data binding */
    printf("[C.51] Graph callback compute texture binding\n"); flush();
    {
        GpuDevice textureDevice;
        GpuDeviceDesc deviceDesc = {
            .appName = "phaseC_compute_texture_binding",
            .enableDebugLayer = true,
            .preferredBackend = backend,
        };
        CHECK(gpuCreateDevice(&deviceDesc, &textureDevice));
        GpuCommandQueue textureQueue;
        CHECK(gpuGetQueue(textureDevice, GPU_QUEUE_TYPE_GRAPHICS, &textureQueue));
        GpuShaderCompiler compiler;
        CHECK(gpuCreateShaderCompiler(textureDevice, &compiler));
        GpuShaderCompileDesc shaderDesc = {
            .sourcePath = "compute_texture_binding_test.slang",
            .entryPoint = "computeMain",
            .target = GPU_SHADER_TARGET_DXIL,
        };
        GpuShaderProgram program = NULL;
        CHECK(gpuCompileShader(compiler, &shaderDesc, &program));
        GpuPipelineHandle pipeline = GPU_NULL_HANDLE;
        CHECK(gpuCreateComputePipelineFromProgram(
            textureDevice, program, "compute_texture_binding", &pipeline));
        const float sourceValues[16] = {
            1, 2, 3, 4, 5, 6, 7, 8,
            9, 10, 11, 12, 13, 14, 15, 16,
        };
        GpuTextureDesc sourceDesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 4, .height = 4, .depth = 1,
            .arrayLength = 1, .mipCount = 1, .format = GPU_FORMAT_R32_FLOAT,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_COPY_DEST,
            .label = "compute_texture_source",
        };
        GpuTextureDesc targetDesc = sourceDesc;
        targetDesc.width = 2;
        targetDesc.height = 2;
        targetDesc.usage = GPU_TEXTURE_USAGE_UNORDERED_ACCESS |
                           GPU_TEXTURE_USAGE_COPY_SOURCE;
        targetDesc.label = "compute_texture_target";
        GpuTextureHandle source;
        GpuTextureHandle target;
        CHECK(gpuCreateTexture(textureDevice, &sourceDesc, &source));
        CHECK(gpuCreateTexture(textureDevice, &targetDesc, &target));
        GpuTextureUploadDesc upload = {
            .data = sourceValues, .dataSize = sizeof(sourceValues),
            .rowPitch = 4 * sizeof(float), .slicePitch = sizeof(sourceValues),
            .mipLevel = 0, .arrayLayer = 0,
        };
        CHECK(gpuUploadTextureData(textureDevice, source, &upload));
        GpuGraph graph;
        CHECK(gpuGraphCreate(textureDevice, &graph));
        GpuGraphResource sourceResource = gpuGraphImportTextureEx(
            graph, source, GPU_RESOURCE_STATE_SHADER_RESOURCE,
            GPU_RESOURCE_STATE_SHADER_RESOURCE, "compute_texture_source");
        GpuGraphResource targetResource = gpuGraphImportTextureEx(
            graph, target, GPU_RESOURCE_STATE_UNORDERED_ACCESS,
            GPU_RESOURCE_STATE_COPY_SOURCE, "compute_texture_target");
        GpuGraphPass pass = gpuGraphAddComputePass(graph, "compute_texture_binding");
        gpuGraphPassRead(pass, sourceResource);
        gpuGraphPassWrite(pass, targetResource);
        TextureBindingTestData callbackData = {
            .source = sourceResource, .target = targetResource,
            .pipeline = pipeline, .result = GPU_ERROR_UNKNOWN,
        };
        gpuGraphPassSetCallback(pass, texture_binding_callback, &callbackData);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, textureQueue));
        CHECK(gpuQueueWaitOnHost(textureQueue));
        CHECK(callbackData.result);
        GpuTextureFootprint footprint = {0};
        CHECK(gpuGetTextureReadbackFootprint(textureDevice, target, 0, &footprint));
        GpuBufferHandle readback;
        CHECK(gpuCreateReadbackBuffer(textureDevice, footprint.totalSize, &readback));
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(textureDevice, textureQueue);
        CHECK_TRUE(encoder != NULL);
        CHECK(gpuCmdCopyTextureToBuffer(encoder, target, 0, 0, readback, 0));
        GpuCommandBuffer commands = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(commands != NULL);
        CHECK(gpuQueueSubmit(textureQueue, 1, &commands));
        CHECK(gpuQueueWaitOnHost(textureQueue));
        void* mapped = NULL;
        CHECK(gpuMapReadbackBuffer(textureDevice, readback, &mapped));
        const float expected[4] = {6, 8, 14, 16};
        for (uint32_t y = 0; y < 2; ++y) {
            const float* row =
                (const float*)((const uint8_t*)mapped + y * footprint.rowPitch);
            for (uint32_t x = 0; x < 2; ++x)
                CHECK_TRUE(row[x] == expected[y * 2 + x]);
        }
        gpuUnmapReadbackBuffer(textureDevice, readback);
        gpuDestroyBuffer(textureDevice, readback);
        gpuGraphDestroy(graph);
        gpuDestroyTexture(textureDevice, target);
        gpuDestroyTexture(textureDevice, source);
        CHECK(gpuDestroyPipeline(textureDevice, pipeline));
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(textureDevice);
    }
    printf("  OK\n"); flush();

    if (isSoftwareVulkanAdapter(device)) {
        printf("[C.30-C.38] Skipped on software Vulkan\n"); flush();
        goto phasec_finish_tests;
    }

    /* C.30 Cross-queue fence sync */
    printf("[C.30] Cross-queue fence sync\n"); flush();
    {
        GpuFence fence;
        CHECK(gpuCreateFence(device, 0, &fence));
        GpuCommandQueue gfxQueue, cmpQueue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &gfxQueue));
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_COMPUTE, &cmpQueue));

        GpuCommandEncoder cmpEnc = gpuBeginCommandEncoder(device, cmpQueue);
        CHECK_TRUE(cmpEnc != NULL);
        GpuComputePassEncoder cp = gpuCmdBeginComputePass(cmpEnc);
        CHECK_TRUE(cp != NULL);
        gpuCmdEndComputePass(cp);
        GpuCommandBuffer cmpCmd = gpuFinishCommandEncoder(cmpEnc);
        CHECK_TRUE(cmpCmd != NULL);

        GpuCommandEncoder gfxEnc = gpuBeginCommandEncoder(device, gfxQueue);
        CHECK_TRUE(gfxEnc != NULL);
        GpuCommandBuffer gfxCmd = gpuFinishCommandEncoder(gfxEnc);
        CHECK_TRUE(gfxCmd != NULL);

        CHECK(gpuSetupComputeToGraphicsSync(
            cmpQueue, cmpCmd, gfxQueue, gfxCmd, fence, 1));
        CHECK(gpuQueueWaitOnHost(gfxQueue));
        CHECK_TRUE(gpuFenceIsCompleted(fence, 1));
        gpuDestroyFence(device, fence);
    }
    printf("  OK\n"); flush();

    /* C.47 Exact graphics-compute-graphics multi-queue content */
    printf("[C.47] Multi-queue content correctness\n"); flush();
    {
        GpuDevice multiQueueDevice;
        GpuDeviceDesc deviceDesc = {
            .appName = "phaseC_multi_queue_content",
            .enableDebugLayer = true,
            .preferredBackend = backend,
        };
        CHECK(gpuCreateDevice(&deviceDesc, &multiQueueDevice));
        GpuCommandQueue graphicsQueue;
        CHECK(gpuGetQueue(
            multiQueueDevice, GPU_QUEUE_TYPE_GRAPHICS, &graphicsQueue));
        GpuShaderCompiler compiler;
        CHECK(gpuCreateShaderCompiler(multiQueueDevice, &compiler));
        GpuShaderCompileDesc shaderDesc = {
            .sourcePath = "graph_binding_test.slang",
            .entryPoint = "computeMain",
            .fragmentEntryPoint = NULL,
            .target = GPU_SHADER_TARGET_DXIL,
        };
        GpuShaderProgram program = NULL;
        CHECK(gpuCompileShader(compiler, &shaderDesc, &program));
        GpuPipelineHandle pipeline = GPU_NULL_HANDLE;
        CHECK(gpuCreateComputePipelineFromProgram(
            multiQueueDevice, program, "multi_queue_compute", &pipeline));

        const uint32_t inputData[4] = {3u, 5u, 8u, 13u};
        GpuBufferDesc sourceDesc = {
            .size = sizeof(inputData),
            .elementSize = sizeof(uint32_t),
            .usage = GPU_BUFFER_USAGE_COPY_SOURCE,
            .label = "multi_queue_source",
        };
        GpuBufferDesc outputDesc = {
            .size = sizeof(inputData),
            .elementSize = sizeof(uint32_t),
            .usage = GPU_BUFFER_USAGE_COPY_DEST |
                     GPU_BUFFER_USAGE_UNORDERED_ACCESS |
                     GPU_BUFFER_USAGE_COPY_SOURCE,
            .label = "multi_queue_output",
        };
        GpuBufferHandle source, output, readback;
        CHECK(gpuCreateBufferInit(
            multiQueueDevice, &sourceDesc, inputData, &source));
        CHECK(gpuCreateBuffer(multiQueueDevice, &outputDesc, &output));
        CHECK(gpuCreateReadbackBuffer(
            multiQueueDevice, 256, &readback));

        GpuGraph graph;
        CHECK(gpuGraphCreate(multiQueueDevice, &graph));
        gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_MULTI_QUEUE);
        GpuGraphResource sourceResource = gpuGraphImportBufferEx(
            graph, source,
            GPU_RESOURCE_STATE_COPY_SOURCE,
            GPU_RESOURCE_STATE_COPY_SOURCE,
            "multi_queue_source");
        GpuGraphResource outputResource = gpuGraphImportBufferEx(
            graph, output,
            GPU_RESOURCE_STATE_COPY_DEST,
            GPU_RESOURCE_STATE_COPY_SOURCE,
            "multi_queue_output");
        GpuGraphResource readbackResource = gpuGraphImportBufferEx(
            graph, readback,
            GPU_RESOURCE_STATE_COPY_DEST,
            GPU_RESOURCE_STATE_COPY_DEST,
            "multi_queue_readback");

        GpuGraphPass upload = gpuGraphAddCopyPass(graph, "multi_queue_upload");
        gpuGraphPassRead(upload, sourceResource);
        gpuGraphPassWrite(upload, outputResource);
        GpuGraphPass compute = gpuGraphAddComputePass(
            graph, "multi_queue_compute");
        gpuGraphPassReadWrite(compute, outputResource);
        GraphBindingTestData callbackData = {
            .output = outputResource,
            .arguments = GPU_GRAPH_NULL_RESOURCE,
            .pipeline = pipeline,
            .result = GPU_ERROR_UNKNOWN,
            .dataResult = GPU_ERROR_UNKNOWN,
        };
        gpuGraphPassSetCallback(compute, graph_binding_callback, &callbackData);
        GpuGraphPass download = gpuGraphAddCopyPass(
            graph, "multi_queue_download");
        gpuGraphPassRead(download, outputResource);
        gpuGraphPassWrite(download, readbackResource);

        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, graphicsQueue));
        CHECK(gpuQueueWaitOnHost(graphicsQueue));
        CHECK(callbackData.result);
        void* mapped = NULL;
        CHECK(gpuMapReadbackBuffer(multiQueueDevice, readback, &mapped));
        const uint32_t* actual = (const uint32_t*)mapped;
        printf("  C.47 output: %u %u %u %u\n",
               actual[0], actual[1], actual[2], actual[3]); flush();
        for (uint32_t i = 0; i < 4; ++i) {
            CHECK_TRUE(actual[i] == i + 41u);
        }
        gpuUnmapReadbackBuffer(multiQueueDevice, readback);

        gpuGraphDestroy(graph);
        gpuDestroyBuffer(multiQueueDevice, readback);
        gpuDestroyBuffer(multiQueueDevice, output);
        gpuDestroyBuffer(multiQueueDevice, source);
        CHECK(gpuDestroyPipeline(multiQueueDevice, pipeline));
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(multiQueueDevice);
    }
    printf("  OK\n"); flush();

    /* C.31 Manual command path hazard validation */
    printf("[C.31] Hazard validation\n"); flush();
    {
        gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_ERROR);
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_COPY_DEST,
            .label = "hazard_buf"
        };
        GpuBufferHandle buf;
        CHECK(gpuCreateBuffer(device, &bdesc, &buf));

        GpuCommandEncoder enc = gpuBeginCommandEncoder(device, queue);
        CHECK_TRUE(enc != NULL);
        CHECK_TRUE(gpuCmdValidateBufferAccess(device, enc, buf, GPU_ACCESS_SHADER_READ) != GPU_SUCCESS);
        fflush(stderr);
        gpuCmdSetBufferState(device, enc, buf, GPU_RESOURCE_STATE_STORAGE_BUFFER);
        CHECK(gpuCmdValidateBufferAccess(device, enc, buf, GPU_ACCESS_SHADER_READ));
        GpuCommandBuffer cmd = gpuFinishCommandEncoder(enc);
        CHECK_TRUE(cmd != NULL);
        CHECK(gpuQueueSubmit(queue, 1, &cmd));
        gpuQueueWaitOnHost(queue);
        gpuDestroyBuffer(device, buf);
        gpuSetDebugLevel(device, GPU_DEBUG_LEVEL_NONE);
    }
    printf("  OK\n"); flush();

    /* C.32 Buffer lifetime aliasing */
    printf("[C.32] Buffer lifetime aliasing\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuBufferDesc bdesc = {
            .size = 4096, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS,
            .label = "alias_buf"
        };
        GpuGraphResource bufA = gpuGraphCreateBuffer(graph, &bdesc, "buf_a");
        GpuGraphResource bufB = gpuGraphCreateBuffer(graph, &bdesc, "buf_b");
        GpuGraphPass p0 = gpuGraphAddComputePass(graph, "writer_a");
        gpuGraphPassWrite(p0, bufA);
        GpuGraphPass p1 = gpuGraphAddComputePass(graph, "writer_b");
        gpuGraphPassWrite(p1, bufB);
        CHECK(gpuGraphCompile(graph));

        GpuGraphResourceLifetimeInfo lifeA, lifeB;
        CHECK(gpuGraphGetResourceLifetime(graph, bufA, &lifeA));
        CHECK(gpuGraphGetResourceLifetime(graph, bufB, &lifeB));
        CHECK_TRUE(lifeA.allocationId == lifeB.allocationId);
        CHECK_TRUE(lifeB.aliased);
        CHECK_TRUE(gpuGraphGetBuffer(graph, bufA).index == gpuGraphGetBuffer(graph, bufB).index);

        GpuTransientAllocationPlan plans[4];
        CHECK_TRUE(gpuGraphBuildTransientAllocationPlan(graph, plans, 4) == 1);
        CHECK_TRUE(plans[0].objectAliased);
        if (gpuDeviceSupportsTransientHeap(device))
            CHECK_TRUE(plans[0].heapPlaced);
        else
            CHECK_TRUE(!plans[0].heapPlaced);

        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.33 Subresource barrier */
    printf("[C.33] Subresource barrier\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 128, .height = 128,
            .depth = 1, .arrayLength = 1, .mipCount = 2,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_UNORDERED_ACCESS | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "mip_tex"
        };
        GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "mip_tex");
        GpuGraphPass w0 = gpuGraphAddComputePass(graph, "write_mip0");
        gpuGraphPassWriteSubresource(w0, tex, 0, 0);
        GpuGraphPass r1 = gpuGraphAddComputePass(graph, "read_mip1");
        gpuGraphPassReadSubresource(r1, tex, 1, 0);
        CHECK(gpuGraphCompile(graph));

        CHECK_TRUE(gpuGraphGetBarrierCount(graph) > 0);
        bool foundSubresource = false;
        for (uint32_t bi = 0; bi < gpuGraphGetBarrierCount(graph); bi++) {
            GpuGraphBarrierInfo info;
            CHECK(gpuGraphGetBarrier(graph, bi, &info));
            if (info.mipCount > 0 && info.mipLevel == 1) foundSubresource = true;
        }
        CHECK_TRUE(foundSubresource);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.34 Pass GPU profiling duration */
    printf("[C.34] Pass profiling duration\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        gpuGraphSetPassProfiling(graph, true);
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS,
            .label = "prof_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "prof_buf");
        GpuGraphPass p0 = gpuGraphAddComputePass(graph, "prof_pass");
        gpuGraphPassWrite(p0, buf);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetPassProfiling(graph));
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);

        float durationMs = -1.0f;
        CHECK(gpuGraphGetPassGpuDurationMs(graph, 0, &durationMs));
        CHECK_TRUE(durationMs >= 0.0f);
        printf("  pass0 duration: %.4f ms\n", durationMs);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.35 Independent queue capability */
    printf("[C.35] Independent queue capability\n"); flush();
    {
        GpuCommandQueue gfxQueue, cmpQueue, xferQueue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &gfxQueue));
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_COMPUTE, &cmpQueue));
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_TRANSFER, &xferQueue));
        if (gpuDeviceSupportsIndependentQueues(device)) {
            CHECK_TRUE(gfxQueue != cmpQueue);
            CHECK_TRUE(gfxQueue != xferQueue);
            CHECK_TRUE(cmpQueue != xferQueue);
        } else {
            CHECK_TRUE(gfxQueue == cmpQueue);
            CHECK_TRUE(gfxQueue == xferQueue);
        }
    }
    printf("  OK\n"); flush();

    /* C.36 Flat barrier plan + async compute graph path */
    printf("[C.36] Flat barrier plan async graph\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_MULTI_QUEUE);
        GpuTextureDesc colorDesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 16, .height = 16,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET,
            .label = "async_color"
        };
        GpuGraphResource color = gpuGraphCreateTexture(graph, &colorDesc, "async_color");
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "async_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "async_buf");
        GpuGraphPass rp = gpuGraphAddRenderPass(graph, "noop_render");
        GpuGraphColorAttachment colorAttachment = {
            .resource = color, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.0f, 0.0f, 0.0f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(rp, 1, &colorAttachment);
        gpuGraphPassSetCallback(rp, noop_pass_callback, NULL);
        GpuGraphPass cp = gpuGraphAddComputePass(graph, "async_compute");
        gpuGraphPassReadWrite(cp, buf);
        GpuGraphPass copyp = gpuGraphAddCopyPass(graph, "async_copy");
        gpuGraphPassWrite(copyp, buf);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetBarrierCount(graph) > 0);
        if (gpuDeviceSupportsIndependentQueues(device)) {
            CHECK_TRUE(gpuGraphGetEffectiveExecuteMode(graph) == GPU_GRAPH_EXECUTE_MULTI_QUEUE);
        } else {
            CHECK_TRUE(gpuGraphGetEffectiveExecuteMode(graph) == GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER);
        }

        GpuGraphBarrierInfo lastBarrier;
        uint32_t bc = gpuGraphGetBarrierCount(graph);
        CHECK(gpuGraphGetBarrier(graph, bc - 1, &lastBarrier));
        CHECK_TRUE(lastBarrier.destPassIndex < gpuGraphGetPassCount(graph));

        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.37 Present barrier chain */
    printf("[C.37] Present barrier chain\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_PRESENT |
                     GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "present_tex"
        };
        GpuGraphResource tex = gpuGraphCreateTexture(graph, &tdesc, "present_tex");
        GpuGraphPass drawPass = gpuGraphAddRenderPass(graph, "draw");
        GpuGraphColorAttachment ca = {
            .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.2f, 0.4f, 0.6f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(drawPass, 1, &ca);
        GpuGraphPass presentPass = gpuGraphAddCopyPass(graph, "present");
        gpuGraphPassPresent(presentPass, tex);
        CHECK(gpuGraphCompile(graph));

        bool foundPresent = false;
        for (uint32_t bi = 0; bi < gpuGraphGetBarrierCount(graph); bi++) {
            GpuGraphBarrierInfo info;
            CHECK(gpuGraphGetBarrier(graph, bi, &info));
            if (info.after == GPU_RESOURCE_STATE_PRESENT) foundPresent = true;
        }
        CHECK_TRUE(foundPresent);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.38 Imported resource final state restore */
    printf("[C.38] Imported final state restore\n"); flush();
    {
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64,
            .depth = 1, .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE |
                     GPU_TEXTURE_USAGE_COPY_SOURCE,
            .label = "restore_tex"
        };
        GpuTextureHandle texHandle;
        CHECK(gpuCreateTexture(device, &tdesc, &texHandle));

        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuGraphResource tex = gpuGraphImportTextureEx(
            graph,
            texHandle,
            GPU_RESOURCE_STATE_SHADER_RESOURCE,
            GPU_RESOURCE_STATE_COPY_SOURCE,
            "restore_tex");
        GpuGraphPass drawPass = gpuGraphAddRenderPass(graph, "draw");
        GpuGraphColorAttachment ca = {
            .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE,
            .clearColor = { 0.1f, 0.1f, 0.1f, 1.0f }
        };
        gpuGraphPassSetColorAttachments(drawPass, 1, &ca);
        CHECK(gpuGraphCompile(graph));

        uint32_t lastPi = gpuGraphGetExecutionOrderPassIndex(graph, gpuGraphGetExecutionOrderCount(graph) - 1);
        bool foundRestore = false;
        for (uint32_t bi = 0; bi < gpuGraphGetPassBarrierCount(graph, lastPi); bi++) {
            GpuGraphBarrierInfo info;
            CHECK(gpuGraphGetPassBarrier(graph, lastPi, bi, &info));
            if (info.after == GPU_RESOURCE_STATE_COPY_SOURCE) foundRestore = true;
        }
        CHECK_TRUE(foundRestore);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        CHECK_TRUE(gpuGetTextureState(device, texHandle) == GPU_RESOURCE_STATE_COPY_SOURCE);
        gpuGraphDestroy(graph);
        gpuDestroyTexture(device, texHandle);
    }
    printf("  OK\n"); flush();

phasec_finish_tests:
    /* C.39 Validation warnings API */
    printf("[C.39] Validation warnings API\n"); flush();
    {
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_MULTI_QUEUE);
        GpuBufferDesc bdesc = {
            .size = 128, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS,
            .label = "warn_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "warn_buf");
        GpuGraphPass cp = gpuGraphAddComputePass(graph, "warn_compute");
        gpuGraphPassWrite(cp, buf);
        CHECK(gpuGraphCompile(graph));
        bool foundDowngrade = false;
        bool foundUninitialized = false;
        for (uint32_t wi = 0; wi < gpuGraphGetValidationWarningCount(graph); wi++) {
            if (strstr(gpuGraphGetValidationWarning(graph, wi), "downgraded") != NULL)
                foundDowngrade = true;
            if (strstr(gpuGraphGetValidationWarning(graph, wi), "uninitialized_access") != NULL)
                foundUninitialized = true;
        }
        CHECK_TRUE(!foundUninitialized);
        if (!gpuDeviceSupportsIndependentQueues(device))
            CHECK_TRUE(foundDowngrade);
        else
            CHECK_TRUE(gpuGraphGetValidationWarningCount(graph) == 0);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    /* C.42 Graph callback resource binding and unified compute dispatch */
    printf("[C.42] Graph callback compute resource binding\n"); flush();
    {
        GpuDevice bindingDevice;
        GpuDeviceDesc deviceDesc = {
            .appName = "phaseC_graph_binding",
            .enableDebugLayer = true,
            .preferredBackend = backend,
        };
        CHECK(gpuCreateDevice(&deviceDesc, &bindingDevice));
        GpuCommandQueue bindingQueue;
        CHECK(gpuGetQueue(bindingDevice, GPU_QUEUE_TYPE_GRAPHICS, &bindingQueue));
        GpuShaderCompiler compiler;
        CHECK(gpuCreateShaderCompiler(bindingDevice, &compiler));
        GpuShaderCompileDesc shaderDesc = {
            .sourcePath = "graph_binding_test.slang",
            .entryPoint = "computeMain",
            .fragmentEntryPoint = NULL,
            .target = GPU_SHADER_TARGET_DXIL,
        };
        GpuShaderProgram program = NULL;
        CHECK(gpuCompileShader(compiler, &shaderDesc, &program));
        GpuPipelineHandle pipeline = GPU_NULL_HANDLE;
        CHECK(gpuCreateComputePipelineFromProgram(
            bindingDevice, program, "graph_binding_compute", &pipeline));

        GpuBufferDesc bufferDesc = {
            .size = 4 * sizeof(uint32_t),
            .elementSize = sizeof(uint32_t),
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS |
                     GPU_BUFFER_USAGE_COPY_SOURCE,
            .label = "graph_binding_output",
        };
        GpuBufferHandle output;
        CHECK(gpuCreateBuffer(bindingDevice, &bufferDesc, &output));
        GpuGraph graph;
        CHECK(gpuGraphCreate(bindingDevice, &graph));
        GpuGraphResource outputResource = gpuGraphImportBufferEx(
            graph,
            output,
            GPU_RESOURCE_STATE_UNORDERED_ACCESS,
            GPU_RESOURCE_STATE_COPY_SOURCE,
            "graph_binding_output");
        GpuGraphPass pass = gpuGraphAddComputePass(graph, "graph_binding_compute");
        gpuGraphPassReadWrite(pass, outputResource);
        GraphBindingTestData callbackData = {
            .output = outputResource,
            .arguments = GPU_GRAPH_NULL_RESOURCE,
            .pipeline = pipeline,
            .result = GPU_ERROR_UNKNOWN,
            .dataResult = GPU_ERROR_UNKNOWN,
        };
        gpuGraphPassSetCallback(pass, graph_binding_callback, &callbackData);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, bindingQueue));
        CHECK(gpuQueueWaitOnHost(bindingQueue));
        CHECK(callbackData.result);
        CHECK_TRUE(gpuGetBufferState(bindingDevice, output) == GPU_RESOURCE_STATE_COPY_SOURCE);

        uint32_t actual[4] = {0};
        CHECK(gpuDownloadFromBuffer(bindingDevice, output, actual, sizeof(actual), 0));
        printf("  C.42 output: %u %u %u %u\n", actual[0], actual[1], actual[2], actual[3]); flush();
        for (uint32_t i = 0; i < 4; ++i) {
            CHECK_TRUE(actual[i] == i + 41);
        }

        gpuGraphDestroy(graph);
        gpuDestroyBuffer(bindingDevice, output);
        CHECK(gpuDestroyPipeline(bindingDevice, pipeline));
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(bindingDevice);
    }
    printf("  OK\n"); flush();

    /* C.43 Graph-tracked indirect argument dispatch */
    printf("[C.43] Graph indirect argument dispatch\n"); flush();
    {
        GpuDevice indirectDevice;
        GpuDeviceDesc deviceDesc = {
            .appName = "phaseC_graph_indirect",
            .enableDebugLayer = true,
            .preferredBackend = backend,
        };
        CHECK(gpuCreateDevice(&deviceDesc, &indirectDevice));
        GpuCommandQueue indirectQueue;
        CHECK(gpuGetQueue(indirectDevice, GPU_QUEUE_TYPE_GRAPHICS, &indirectQueue));
        GpuShaderCompiler compiler;
        CHECK(gpuCreateShaderCompiler(indirectDevice, &compiler));
        GpuShaderCompileDesc shaderDesc = {
            .sourcePath = "graph_binding_test.slang",
            .entryPoint = "computeMain",
            .fragmentEntryPoint = NULL,
            .target = GPU_SHADER_TARGET_DXIL,
        };
        GpuShaderProgram program = NULL;
        CHECK(gpuCompileShader(compiler, &shaderDesc, &program));
        GpuPipelineHandle pipeline = GPU_NULL_HANDLE;
        CHECK(gpuCreateComputePipelineFromProgram(
            indirectDevice, program, "graph_indirect_compute", &pipeline));

        GpuBufferDesc outputDesc = {
            .size = 4 * sizeof(uint32_t),
            .elementSize = sizeof(uint32_t),
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS |
                     GPU_BUFFER_USAGE_COPY_SOURCE,
            .label = "graph_indirect_output",
        };
        GpuBufferHandle output;
        CHECK(gpuCreateBuffer(indirectDevice, &outputDesc, &output));
        const uint32_t dispatchArguments[3] = {4, 1, 1};
        GpuBufferDesc argumentDesc = {
            .size = sizeof(dispatchArguments),
            .elementSize = sizeof(uint32_t),
            .usage = GPU_BUFFER_USAGE_INDIRECT_ARGUMENT |
                     GPU_BUFFER_USAGE_COPY_DEST,
            .label = "graph_indirect_arguments",
        };
        GpuBufferHandle arguments;
        CHECK(gpuCreateBufferInit(
            indirectDevice, &argumentDesc, dispatchArguments, &arguments));

        GpuGraph graph;
        CHECK(gpuGraphCreate(indirectDevice, &graph));
        GpuGraphResource outputResource = gpuGraphImportBufferEx(
            graph,
            output,
            GPU_RESOURCE_STATE_UNORDERED_ACCESS,
            GPU_RESOURCE_STATE_COPY_SOURCE,
            "graph_indirect_output");
        GpuGraphResource argumentResource = gpuGraphImportBufferEx(
            graph,
            arguments,
            GPU_RESOURCE_STATE_COMMON,
            GPU_RESOURCE_STATE_INDIRECT_ARGUMENT,
            "graph_indirect_arguments");
        GpuGraphPass pass = gpuGraphAddComputePass(graph, "graph_indirect_compute");
        gpuGraphPassReadWrite(pass, outputResource);
        gpuGraphPassReadIndirect(pass, argumentResource);
        GraphBindingTestData callbackData = {
            .output = outputResource,
            .arguments = argumentResource,
            .pipeline = pipeline,
            .result = GPU_ERROR_UNKNOWN,
            .dataResult = GPU_ERROR_UNKNOWN,
        };
        gpuGraphPassSetCallback(pass, graph_binding_callback, &callbackData);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, indirectQueue));
        CHECK(gpuQueueWaitOnHost(indirectQueue));
        CHECK(callbackData.result);
        CHECK_TRUE(gpuGetBufferState(indirectDevice, arguments) == GPU_RESOURCE_STATE_INDIRECT_ARGUMENT);

        uint32_t actual[4] = {0};
        CHECK(gpuDownloadFromBuffer(indirectDevice, output, actual, sizeof(actual), 0));
        for (uint32_t i = 0; i < 4; ++i) {
            CHECK_TRUE(actual[i] == i + 41);
        }

        gpuGraphDestroy(graph);
        gpuDestroyBuffer(indirectDevice, arguments);
        gpuDestroyBuffer(indirectDevice, output);
        CHECK(gpuDestroyPipeline(indirectDevice, pipeline));
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(indirectDevice);
    }
    printf("  OK\n"); flush();

    /* C.44 Imported copy-destination first write is not a graph hazard */
    printf("[C.44] Imported first-write hazard classification\n"); flush();
    {
        GpuBufferDesc bdesc = {
            .size = 128, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_COPY_DEST,
            .label = "first_write_buf"
        };
        GpuBufferHandle handle;
        CHECK(gpuCreateBuffer(device, &bdesc, &handle));
        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuGraphResource buffer = gpuGraphImportBufferEx(
            graph,
            handle,
            GPU_RESOURCE_STATE_COPY_DEST,
            GPU_RESOURCE_STATE_UNORDERED_ACCESS,
            "first_write_buf");
        GpuGraphPass pass = gpuGraphAddComputePass(graph, "first_write");
        gpuGraphPassWrite(pass, buffer);
        gpuGraphPassSetCallback(pass, noop_pass_callback, NULL);
        CHECK(gpuGraphCompile(graph));
        CHECK_TRUE(gpuGraphGetValidationWarningCount(graph) == 0);
        CHECK(gpuGraphExecute(graph, queue));
        CHECK(gpuQueueWaitOnHost(queue));
        CHECK_TRUE(gpuGetBufferState(device, handle) == GPU_RESOURCE_STATE_UNORDERED_ACCESS);
        gpuGraphDestroy(graph);
        gpuDestroyBuffer(device, handle);
    }
    printf("  OK\n"); flush();

    /* C.45 Bit-exact unsigned integer attachment clear */
    printf("[C.45] Typed uint attachment clear\n"); flush();
    {
        GpuDevice clearDevice;
        GpuDeviceDesc deviceDesc = {
            .appName = "phaseC_typed_clear",
            .enableDebugLayer = true,
            .preferredBackend = backend,
        };
        CHECK(gpuCreateDevice(&deviceDesc, &clearDevice));
        GpuCommandQueue clearQueue;
        CHECK(gpuGetQueue(clearDevice, GPU_QUEUE_TYPE_GRAPHICS, &clearQueue));
        GpuTextureDesc textureDesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = 4,
            .height = 4,
            .depth = 1,
            .arrayLength = 1,
            .mipCount = 1,
            .format = GPU_FORMAT_R32_UINT,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET |
                     GPU_TEXTURE_USAGE_UNORDERED_ACCESS |
                     GPU_TEXTURE_USAGE_COPY_SOURCE,
            .label = "typed_uint_clear",
        };
        GpuTextureHandle texture;
        CHECK(gpuCreateTexture(clearDevice, &textureDesc, &texture));
        GpuGraph graph;
        CHECK(gpuGraphCreate(clearDevice, &graph));
        GpuGraphResource resource = gpuGraphImportTextureEx(
            graph,
            texture,
            GPU_RESOURCE_STATE_UNORDERED_ACCESS,
            GPU_RESOURCE_STATE_COPY_SOURCE,
            "typed_uint_clear");
        GpuGraphPass pass = gpuGraphAddRenderPass(graph, "typed_uint_clear");
        GpuGraphColorAttachment attachment = {
            .resource = resource,
            .loadOp = GPU_LOAD_OP_CLEAR,
            .storeOp = GPU_STORE_OP_STORE,
            .clearType = GPU_GRAPH_CLEAR_UINT,
            .clearUint = {0xffffffffu, 0u, 0u, 0u},
        };
        gpuGraphPassSetColorAttachments(pass, 1, &attachment);
        CHECK(gpuGraphCompile(graph));
        CHECK(gpuGraphExecute(graph, clearQueue));
        CHECK(gpuQueueWaitOnHost(clearQueue));

        GpuTextureFootprint footprint = {0};
        CHECK(gpuGetTextureReadbackFootprint(clearDevice, texture, 0, &footprint));
        GpuBufferHandle readback;
        CHECK(gpuCreateReadbackBuffer(clearDevice, footprint.totalSize, &readback));
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(clearDevice, clearQueue);
        CHECK_TRUE(encoder != NULL);
        CHECK(gpuCmdCopyTextureToBuffer(encoder, texture, 0, 0, readback, 0));
        GpuCommandBuffer commands = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(commands != NULL);
        CHECK(gpuQueueSubmit(clearQueue, 1, &commands));
        CHECK(gpuQueueWaitOnHost(clearQueue));
        void* mapped = NULL;
        CHECK(gpuMapReadbackBuffer(clearDevice, readback, &mapped));
        for (uint32_t y = 0; y < textureDesc.height; ++y) {
            const uint32_t* row =
                (const uint32_t*)((const uint8_t*)mapped + y * footprint.rowPitch);
            for (uint32_t x = 0; x < textureDesc.width; ++x) {
                CHECK_TRUE(row[x] == 0xffffffffu);
            }
        }
        gpuUnmapReadbackBuffer(clearDevice, readback);
        gpuDestroyBuffer(clearDevice, readback);
        gpuGraphDestroy(graph);
        gpuDestroyTexture(clearDevice, texture);
        gpuDestroyDevice(clearDevice);
    }
    printf("  OK\n"); flush();

    if (isSoftwareVulkanAdapter(device)) {
        printf("\nALL PASSED\n"); flush();
        _exit(0);
    }
    gpuDestroyDevice(device);
    printf("\nALL PASSED\n"); flush();
    return 0;
}
