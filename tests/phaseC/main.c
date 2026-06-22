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

    gpuDestroyDevice(device);
    printf("\nALL PASSED\n"); flush();
    return 0;
}
