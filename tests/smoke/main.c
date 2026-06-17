#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(expr) do { \
    GpuResult _r = (expr); \
    if (_r != GPU_SUCCESS) { \
        fprintf(stderr, "FAIL: %s returned %d at %s:%d\n", #expr, _r, __FILE__, __LINE__); \
        return 1; \
    } \
} while(0)

int main(void)
{
    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "smoke_test", .enableDebugLayer = false };
    CHECK(gpuCreateDevice(&devDesc, &device));

    GpuBufferDesc bufDesc = {
        .size = 256,
        .elementSize = 4,
        .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE,
        .label = "test_buf",
    };
    GpuBufferHandle buf;
    CHECK(gpuCreateBuffer(device, &bufDesc, &buf));
    if (!gpuHandleIsValid(buf)) { fprintf(stderr, "FAIL: buffer handle invalid\n"); return 1; }

    GpuBufferHandle bufInit;
    uint32_t initData[64];
    for (int i = 0; i < 64; i++) initData[i] = (uint32_t)i;
    GpuBufferDesc bufInitDesc = {
        .size = sizeof(initData),
        .elementSize = 4,
        .usage = GPU_BUFFER_USAGE_VERTEX_BUFFER | GPU_BUFFER_USAGE_COPY_DEST,
        .label = "init_buf",
    };
    CHECK(gpuCreateBufferInit(device, &bufInitDesc, initData, &bufInit));
    if (!gpuHandleIsValid(bufInit)) { fprintf(stderr, "FAIL: init buffer handle invalid\n"); return 1; }

    GpuTextureDesc texDesc = {
        .type = GPU_TEXTURE_TYPE_2D,
        .width = 64,
        .height = 64,
        .depth = 1,
        .arrayLength = 1,
        .mipCount = 1,
        .format = GPU_FORMAT_RGBA8_UNORM,
        .sampleCount = 1,
        .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_RENDER_TARGET,
        .label = "test_tex",
    };
    GpuTextureHandle tex;
    CHECK(gpuCreateTexture(device, &texDesc, &tex));
    if (!gpuHandleIsValid(tex)) { fprintf(stderr, "FAIL: texture handle invalid\n"); return 1; }

    uint32_t oldGen = buf.generation;
    CHECK(gpuDestroyBuffer(device, buf));
    GpuBufferHandle buf2;
    CHECK(gpuCreateBuffer(device, &bufDesc, &buf2));
    if (buf2.index != buf.index) { fprintf(stderr, "FAIL: slot reuse expected\n"); return 1; }
    if (buf2.generation <= oldGen) { fprintf(stderr, "FAIL: generation should increase after destroy+alloc\n"); return 1; }

    CHECK(gpuDestroyBuffer(device, buf2));
    CHECK(gpuDestroyBuffer(device, bufInit));
    CHECK(gpuDestroyTexture(device, tex));
    gpuDestroyDevice(device);

    printf("ALL PASSED\n");
    return 0;
}
