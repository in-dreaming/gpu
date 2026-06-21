#include "gpu/gpu.h"
#include <stdint.h>
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
    if (gpuGetTextureState(device, tex) != GPU_RESOURCE_STATE_RENDER_TARGET) {
        fprintf(stderr, "FAIL: texture default state should follow usage\n");
        return 1;
    }

    GpuTextureDesc streamTexDesc = texDesc;
    streamTexDesc.width = 2;
    streamTexDesc.height = 2;
    streamTexDesc.usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_COPY_DEST;
    streamTexDesc.label = "stream_tex";
    GpuTextureHandle streamTex;
    CHECK(gpuCreateTexture(device, &streamTexDesc, &streamTex));

    GpuStreamingManager streamer;
    CHECK(gpuStreamingManagerCreate(device, 64, &streamer));
    uint32_t tinyUpload[4] = {0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu};
    GpuResult streamRes = gpuStreamingRequest(streamer, streamTex, 0, 2, tinyUpload, sizeof(tinyUpload));
    if (streamRes != GPU_ERROR_INVALID_ARGS) {
        fprintf(stderr, "FAIL: streaming multi-mip request should be rejected without layout info\n");
        return 1;
    }
    streamRes = gpuStreamingRequest(streamer, streamTex, 0, 1, tinyUpload, sizeof(tinyUpload));
    if (streamRes != GPU_SUCCESS) {
        fprintf(stderr, "FAIL: streaming single-mip request failed: %d\n", streamRes);
        return 1;
    }
    CHECK(gpuStreamingUpdate(streamer));
    if (gpuGetTextureState(device, streamTex) != GPU_RESOURCE_STATE_SHADER_RESOURCE) {
        fprintf(stderr, "FAIL: streaming update should leave texture shader-readable\n");
        return 1;
    }
    gpuStreamingManagerDestroy(streamer);

    GpuSparseTextureProperties sparseProps;
    GpuResult sparseRes = gpuGetSparseTextureProperties(device, GPU_FORMAT_RGBA8_UNORM, 64, 64, 1, 1, &sparseProps);
    if (sparseRes != GPU_ERROR_NOT_SUPPORTED) {
        fprintf(stderr, "FAIL: sparse texture properties should report unsupported on this backend contract\n");
        return 1;
    }

    GpuBindlessHeap heap;
    GpuBindlessHeapDesc heapDesc = {
        .maxDescriptors = 16,
        .descriptorType = GPU_DESCRIPTOR_TYPE_BUFFER,
    };
    CHECK(gpuCreateBindlessHeap(device, &heapDesc, &heap));
    uint32_t bindlessIndex = gpuBindlessAllocate(heap, buf);
    if (bindlessIndex == UINT32_MAX) { fprintf(stderr, "FAIL: bindless buffer allocation failed\n"); return 1; }
    if (!gpuBindlessIsAllocated(heap, bindlessIndex)) { fprintf(stderr, "FAIL: bindless slot not marked allocated\n"); return 1; }
    GpuHandle bindlessResource;
    CHECK(gpuBindlessGetResource(heap, bindlessIndex, &bindlessResource));
    if (bindlessResource.index != buf.index || bindlessResource.generation != buf.generation) {
        fprintf(stderr, "FAIL: bindless slot resource mismatch\n");
        return 1;
    }
    GpuDescriptorHandleInfo descInfo;
    CHECK(gpuBindlessGetDescriptorHandle(heap, bindlessIndex, &descInfo));
    if (descInfo.type != 1) { fprintf(stderr, "FAIL: bindless descriptor type should be Buffer\n"); return 1; }
    gpuBindlessFree(heap, bindlessIndex);
    if (gpuBindlessIsAllocated(heap, bindlessIndex)) { fprintf(stderr, "FAIL: bindless slot still allocated after free\n"); return 1; }
    gpuDestroyBindlessHeap(heap);

    GpuCommandQueue queue;
    CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));
    GpuCommandEncoder enc = gpuBeginCommandEncoder(device, queue);
    if (!enc) { fprintf(stderr, "FAIL: command encoder create failed\n"); return 1; }
    gpuCmdSetBufferState(device, enc, buf, GPU_RESOURCE_STATE_COPY_SOURCE);
    GpuCommandBuffer cmd = gpuFinishCommandEncoder(enc);
    if (!cmd) { fprintf(stderr, "FAIL: command buffer finish failed\n"); return 1; }
    CHECK(gpuQueueSubmit(queue, 1, &cmd));
    if (gpuGetBufferState(device, buf) != GPU_RESOURCE_STATE_COPY_SOURCE) {
        fprintf(stderr, "FAIL: buffer state tracking did not update\n");
        return 1;
    }

    uint32_t oldGen = buf.generation;
    CHECK(gpuDestroyBuffer(device, buf));
    GpuBufferHandle buf2;
    CHECK(gpuCreateBuffer(device, &bufDesc, &buf2));
    if (buf2.index != buf.index) { fprintf(stderr, "FAIL: slot reuse expected\n"); return 1; }
    if (buf2.generation <= oldGen) { fprintf(stderr, "FAIL: generation should increase after destroy+alloc\n"); return 1; }

    CHECK(gpuDestroyBuffer(device, buf2));
    CHECK(gpuDestroyBuffer(device, bufInit));
    CHECK(gpuDestroyTexture(device, streamTex));
    CHECK(gpuDestroyTexture(device, tex));
    gpuDestroyDevice(device);

    printf("ALL PASSED\n");
    return 0;
}
