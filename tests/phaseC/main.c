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

#define CHECK_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: %s at %s:%d\n", #expr, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

static void flush(void) { fflush(stdout); fflush(stderr); }

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

int main(void)
{
    printf("=== Phase C: Render Graph Test ===\n\n"); flush();

    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "phaseC_test", .enableDebugLayer = false };
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

        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

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

        CHECK(gpuSetupComputeToGraphicsSync(cmpQueue, cmpCmd, gfxQueue, gfxCmd, fence, 1));
        gpuQueueWaitOnHost(gfxQueue);
        CHECK_TRUE(gpuFenceIsCompleted(fence, 1));
        gpuDestroyFence(device, fence);
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
        GpuBufferDesc bdesc = {
            .size = 256, .elementSize = 4,
            .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_SHADER_RESOURCE,
            .label = "async_buf"
        };
        GpuGraphResource buf = gpuGraphCreateBuffer(graph, &bdesc, "async_buf");
        GpuGraphPass rp = gpuGraphAddRenderPass(graph, "noop_render");
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
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "restore_tex"
        };
        GpuTextureHandle texHandle;
        CHECK(gpuCreateTexture(device, &tdesc, &texHandle));

        GpuGraph graph;
        CHECK(gpuGraphCreate(device, &graph));
        GpuGraphResource tex = gpuGraphImportTexture(graph, texHandle, GPU_RESOURCE_STATE_SHADER_RESOURCE, "restore_tex");
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
            if (info.after == GPU_RESOURCE_STATE_SHADER_RESOURCE) foundRestore = true;
        }
        CHECK_TRUE(foundRestore);
        CHECK(gpuGraphExecute(graph, queue));
        gpuQueueWaitOnHost(queue);
        gpuGraphDestroy(graph);
        gpuDestroyTexture(device, texHandle);
    }
    printf("  OK\n"); flush();

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
        CHECK_TRUE(gpuGraphGetValidationWarningCount(graph) > 0);
        bool foundDowngrade = false;
        for (uint32_t wi = 0; wi < gpuGraphGetValidationWarningCount(graph); wi++) {
            if (strstr(gpuGraphGetValidationWarning(graph, wi), "downgraded") != NULL)
                foundDowngrade = true;
        }
        if (!gpuDeviceSupportsIndependentQueues(device))
            CHECK_TRUE(foundDowngrade);
        else
            CHECK_TRUE(!foundDowngrade);
        gpuGraphDestroy(graph);
    }
    printf("  OK\n"); flush();

    gpuDestroyDevice(device);
    printf("\nALL PASSED\n"); flush();
    return 0;
}
