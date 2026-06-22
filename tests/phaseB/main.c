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

int main(void)
{
    printf("=== Phase B: Lifecycle & Synchronization Test ===\n\n"); flush();

    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "phaseB_test", .enableDebugLayer = false };
    CHECK(gpuCreateDevice(&devDesc, &device));

    GpuCommandQueue queue;
    CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

    /* ---------------------------------------------------------------
     * B.1 Frame Context Create / Destroy
     * --------------------------------------------------------------- */
    printf("[B.1] Frame Context Create/Destroy\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));
        CHECK_TRUE(gpuFrameGetIndex(ctx) == 0);
        CHECK_TRUE(gpuFrameGetInFlightCount(ctx) == 0);
        CHECK_TRUE(gpuGetFrameContext(device) == ctx);

        gpuFrameContextDestroy(ctx);
        CHECK_TRUE(gpuGetFrameContext(device) == NULL);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.2 Frame Begin / End Cycle
     * --------------------------------------------------------------- */
    printf("[B.2] Frame Begin/End Cycle\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 3, &ctx));

        for (int i = 0; i < 5; i++) {
            CHECK(gpuFrameBegin(ctx));
            CHECK_TRUE(gpuFrameGetIndex(ctx) == (uint64_t)(i + 1));
            CHECK(gpuFrameEnd(ctx, queue));
        }

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.3 Deferred Buffer Destroy
     * --------------------------------------------------------------- */
    printf("[B.3] Deferred Buffer Destroy\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));

        CHECK(gpuFrameBegin(ctx));

        GpuBufferDesc desc = { .size = 256, .elementSize = 4, .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE, .label = "deferred_buf" };
        GpuBufferHandle buf;
        CHECK(gpuCreateBuffer(device, &desc, &buf));
        CHECK(gpuDestroyBuffer(device, buf));

        GpuBufferDesc desc2 = { .size = 128, .elementSize = 4, .usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER, .label = "deferred_buf2" };
        GpuBufferHandle buf2;
        CHECK(gpuCreateBuffer(device, &desc2, &buf2));
        CHECK(gpuDestroyBuffer(device, buf2));

        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.4 Deferred Texture Destroy
     * --------------------------------------------------------------- */
    printf("[B.4] Deferred Texture Destroy\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));

        CHECK(gpuFrameBegin(ctx));

        GpuTextureDesc tdesc = { .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64, .depth = 1, .arrayLength = 1, .mipCount = 1, .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1, .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE, .label = "deferred_tex" };
        GpuTextureHandle tex;
        CHECK(gpuCreateTexture(device, &tdesc, &tex));
        CHECK(gpuDestroyTexture(device, tex));

        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.5 Upload Ring
     * --------------------------------------------------------------- */
    printf("[B.5] Upload Ring\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));

        CHECK(gpuFrameBegin(ctx));

        GpuBufferDesc desc = { .size = 1024, .elementSize = 4, .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_COPY_DEST, .label = "upload_target" };
        GpuBufferHandle dst;
        CHECK(gpuCreateBuffer(device, &desc, &dst));

        uint32_t data[256];
        for (int i = 0; i < 256; i++) data[i] = (uint32_t)i;
        CHECK(gpuFrameUploadData(ctx, dst, 0, sizeof(data), data));

        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.6 Subresource State Tracker
     * --------------------------------------------------------------- */
    printf("[B.6] Subresource State Tracker\n"); flush();
    {
        GpuTextureDesc tdesc = { .type = GPU_TEXTURE_TYPE_2D, .width = 64, .height = 64, .depth = 1, .arrayLength = 2, .mipCount = 3, .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1, .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE, .label = "subres_tex" };
        GpuTextureHandle tex;
        CHECK(gpuCreateTexture(device, &tdesc, &tex));

        GpuSubresourceTracker tracker;
        CHECK(gpuSubresourceTrackerCreate(device, tex, &tracker));

        CHECK_TRUE(gpuSubresourceTrackerGet(tracker, 0, 0) == GPU_RESOURCE_STATE_UNDEFINED);

        gpuSubresourceTrackerSet(tracker, 0, 0, GPU_RESOURCE_STATE_RENDER_TARGET);
        gpuSubresourceTrackerSet(tracker, 1, 0, GPU_RESOURCE_STATE_SHADER_RESOURCE);
        gpuSubresourceTrackerSet(tracker, 2, 1, GPU_RESOURCE_STATE_COPY_DEST);

        CHECK_TRUE(gpuSubresourceTrackerGet(tracker, 0, 0) == GPU_RESOURCE_STATE_RENDER_TARGET);
        CHECK_TRUE(gpuSubresourceTrackerGet(tracker, 1, 0) == GPU_RESOURCE_STATE_SHADER_RESOURCE);
        CHECK_TRUE(gpuSubresourceTrackerGet(tracker, 2, 1) == GPU_RESOURCE_STATE_COPY_DEST);
        CHECK_TRUE(gpuSubresourceTrackerGet(tracker, 0, 1) == GPU_RESOURCE_STATE_UNDEFINED);

        gpuSubresourceTrackerDestroy(tracker);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.7 Fence / Completed Value
     * --------------------------------------------------------------- */
    printf("[B.7] Fence and Completed Value\n"); flush();
    {
        GpuResult r = gpuFenceWait(device, NULL, 0, 0);
        CHECK_TRUE(r == GPU_ERROR_INVALID_ARGS);

        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        gpuQueueWaitOnHost(queue);

        uint64_t completed = gpuFrameGetCompletedFenceValue(ctx);
        CHECK_TRUE(completed >= 1);

        printf("  completed=%llu\n", (unsigned long long)completed);

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.8 gpuSetFrameContext / gpuGetFrameContext
     * --------------------------------------------------------------- */
    printf("[B.8] Set/Get Frame Context\n"); flush();
    {
        CHECK_TRUE(gpuGetFrameContext(device) == NULL);

        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));
        CHECK_TRUE(gpuGetFrameContext(device) == ctx);

        gpuSetFrameContext(device, NULL);
        CHECK_TRUE(gpuGetFrameContext(device) == NULL);

        gpuSetFrameContext(device, ctx);
        CHECK_TRUE(gpuGetFrameContext(device) == ctx);

        gpuFrameContextDestroy(ctx);
        CHECK_TRUE(gpuGetFrameContext(device) == NULL);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.9 Stress: 1000 frames of buffer create/mapped-write/destroy
     * --------------------------------------------------------------- */
    printf("[B.9] Stress: 1000 frames churn\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 3, &ctx));

        for (int f = 0; f < 1000; f++) {
            CHECK(gpuFrameBegin(ctx));

            GpuBufferDesc desc = { .size = 64, .elementSize = 4, .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_COPY_DEST, .label = "churn_buf" };
            GpuBufferHandle buf;
            if (gpuCreateBuffer(device, &desc, &buf) == GPU_SUCCESS) {
                uint32_t val = (uint32_t)f;
                gpuFrameUploadData(ctx, buf, 0, sizeof(val), &val);
                gpuDestroyBuffer(device, buf);
            }

            CHECK(gpuFrameEnd(ctx, queue));
        }

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.10 Multiple in-flight frames with overlapping resources
     * --------------------------------------------------------------- */
    printf("[B.10] Multiple in-flight frames\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 3, &ctx));

        GpuBufferHandle bufs[6];
        for (int i = 0; i < 6; i++) {
            GpuBufferDesc desc = { .size = 256, .elementSize = 4, .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_COPY_DEST, .label = "inflight_buf" };
            CHECK(gpuCreateBuffer(device, &desc, &bufs[i]));
        }

        for (int f = 0; f < 6; f++) {
            CHECK(gpuFrameBegin(ctx));
            uint32_t val = (uint32_t)f;
            gpuFrameUploadData(ctx, bufs[f % 6], 0, sizeof(val), &val);
            if (f >= 3) {
                gpuDestroyBuffer(device, bufs[f % 6]);
                bufs[f % 6].index = 0;
                bufs[f % 6].generation = 0;
            }
            CHECK(gpuFrameEnd(ctx, queue));
        }

        for (int i = 0; i < 6; i++) {
            if (bufs[i].index != 0) {
                gpuDestroyBuffer(device, bufs[i]);
            }
        }

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.11 Deferred TextureView destroy
     * --------------------------------------------------------------- */
    printf("[B.11] Deferred TextureView Destroy\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));

        GpuTextureDesc tdesc = { .type = GPU_TEXTURE_TYPE_2D, .width = 32, .height = 32, .depth = 1, .arrayLength = 1, .mipCount = 1, .format = GPU_FORMAT_RGBA8_UNORM, .sampleCount = 1, .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_RENDER_TARGET, .label = "view_src" };
        GpuTextureHandle tex;
        CHECK(gpuCreateTexture(device, &tdesc, &tex));

        GpuTextureHandle viewHandle;
        CHECK(gpuCreateTextureView(device, tex, GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &viewHandle));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuDestroyTextureView(device, viewHandle));
        CHECK(gpuDestroyTexture(device, tex));
        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        gpuFrameContextDestroy(ctx);
    }
    printf("  OK\n"); flush();

    /* ---------------------------------------------------------------
     * B.12 Frame context without device integration (manual defer)
     * --------------------------------------------------------------- */
    printf("[B.12] Manual defer destroy (without device auto)\n"); flush();
    {
        GpuFrameContext ctx;
        CHECK(gpuFrameContextCreate(device, 2, &ctx));
        gpuSetFrameContext(device, NULL);

        GpuBufferDesc desc = { .size = 128, .elementSize = 4, .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE, .label = "manual_defer" };
        GpuBufferHandle buf;
        CHECK(gpuCreateBuffer(device, &desc, &buf));

        CHECK(gpuFrameBegin(ctx));
        gpuFrameDeferDestroyBuffer(ctx, buf);
        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        CHECK(gpuFrameBegin(ctx));
        CHECK(gpuFrameEnd(ctx, queue));

        gpuFrameContextDestroy(ctx);
        gpuSetFrameContext(device, NULL);
    }
    printf("  OK\n"); flush();

    gpuDestroyDevice(device);
    printf("\nALL PASSED\n"); flush();
    return 0;
}
