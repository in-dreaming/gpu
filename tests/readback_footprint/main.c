#include "gpu/gpu.h"
#include <stdio.h>

#define CHECK(expr) do { \
    GpuResult _result = (expr); \
    if (_result != GPU_SUCCESS) { \
        fprintf(stderr, "FAIL: line %d: %s returned %d\n", __LINE__, #expr, _result); \
        return 1; \
    } \
} while (0)

#define CHECK_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL: line %d: %s is false\n", __LINE__, #expr); \
        return 1; \
    } \
} while (0)

int main(void)
{
    GpuTextureDesc desc = {
        .type = GPU_TEXTURE_TYPE_2D,
        .width = 17, .height = 3, .depth = 1,
        .arrayLength = 1, .mipCount = 2,
        .format = GPU_FORMAT_RGBA16_FLOAT,
        .sampleCount = 1,
        .usage = GPU_TEXTURE_USAGE_COPY_SOURCE,
        .label = "descriptor_footprint",
    };
    GpuTextureFootprint footprint = {0};
    CHECK(gpuGetTextureDescReadbackFootprint(&desc, 1, &footprint));
    CHECK_TRUE(footprint.format == GPU_FORMAT_RGBA16_FLOAT);
    CHECK_TRUE(footprint.width == 8);
    CHECK_TRUE(footprint.height == 1);
    CHECK_TRUE(footprint.depth == 1);
    CHECK_TRUE(footprint.rowPitch == 256u);
    CHECK_TRUE(footprint.slicePitch == 256u);
    CHECK_TRUE(footprint.totalSize == 256u);

    desc.format = GPU_FORMAT_R32_UINT;
    CHECK(gpuGetTextureDescReadbackFootprint(&desc, 0, &footprint));
    CHECK_TRUE(footprint.rowPitch == 256u);
    CHECK_TRUE(footprint.totalSize == 3u * 256u);
    CHECK_TRUE(gpuGetTextureDescReadbackFootprint(&desc, 2, &footprint) ==
               GPU_ERROR_INVALID_ARGS);

    puts("readback descriptor footprint verified");
    return 0;
}
