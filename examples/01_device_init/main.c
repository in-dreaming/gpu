#include "gpu/gpu.h"
#include <stdio.h>
#include <string.h>

int main(void)
{
    GpuDevice device;
    GpuDeviceDesc devDesc = {
        .appName = "01_device_init",
        .enableDebugLayer = false,
    };
    GpuResult res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "Failed to create device: %d\n", res);
        return 1;
    }
    fprintf(stderr, "Device created successfully\n");

    GpuBufferDesc bufDesc = {
        .size = 1024 * 1024,
        .elementSize = 0,
        .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE,
        .label = "test_buffer",
    };
    GpuBufferHandle buf;
    res = gpuCreateBuffer(device, &bufDesc, &buf);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "Buffer create failed: %d\n", res);
        gpuDestroyDevice(device);
        return 1;
    }
    fprintf(stderr, "Buffer created: index=%u gen=%u\n", buf.index, buf.generation);

    GpuBufferHandle bufInit;
    uint32_t initData[64];
    memset(initData, 0xAB, sizeof(initData));
    GpuBufferDesc bufInitDesc = {
        .size = sizeof(initData),
        .elementSize = 4,
        .usage = GPU_BUFFER_USAGE_VERTEX_BUFFER | GPU_BUFFER_USAGE_COPY_DEST,
        .label = "init_buffer",
    };
    res = gpuCreateBufferInit(device, &bufInitDesc, initData, &bufInit);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "BufferInit create failed: %d\n", res);
        gpuDestroyBuffer(device, buf);
        gpuDestroyDevice(device);
        return 1;
    }
    fprintf(stderr, "InitBuffer created: index=%u gen=%u\n", bufInit.index, bufInit.generation);

    GpuTextureDesc texDesc = {
        .type = GPU_TEXTURE_TYPE_2D,
        .width = 256,
        .height = 256,
        .depth = 1,
        .arrayLength = 1,
        .mipCount = 1,
        .format = GPU_FORMAT_RGBA8_UNORM,
        .sampleCount = 1,
        .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_RENDER_TARGET,
        .label = "test_texture",
    };
    GpuTextureHandle tex;
    res = gpuCreateTexture(device, &texDesc, &tex);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "Texture create failed: %d\n", res);
        gpuDestroyBuffer(device, bufInit);
        gpuDestroyBuffer(device, buf);
        gpuDestroyDevice(device);
        return 1;
    }
    fprintf(stderr, "Texture created: index=%u gen=%u\n", tex.index, tex.generation);

    uint32_t oldBufGen = buf.generation;
    gpuDestroyBuffer(device, buf);
    fprintf(stderr, "Buffer destroyed\n");

    GpuBufferHandle buf2;
    res = gpuCreateBuffer(device, &bufDesc, &buf2);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "Second buffer create failed: %d\n", res);
    } else {
        fprintf(stderr, "Second buffer: index=%u gen=%u (reused=%d gen_increased=%d)\n",
                buf2.index, buf2.generation,
                buf2.index == buf.index ? 1 : 0,
                buf2.generation > oldBufGen ? 1 : 0);
        gpuDestroyBuffer(device, buf2);
    }

    gpuDestroyBuffer(device, bufInit);
    gpuDestroyTexture(device, tex);
    gpuDestroyDevice(device);
    fprintf(stderr, "Cleanup done\n");
    return 0;
}
