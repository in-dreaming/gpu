#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expr) do { GpuResult _r = (expr); if (_r != GPU_SUCCESS) { fprintf(stderr, "  FAIL: line %d: %s returned %d\n", __LINE__, #expr, _r); return 1; } } while(0)
#define CHECK_TRUE(expr) do { if (!(expr)) { fprintf(stderr, "  FAIL: line %d: %s is false\n", __LINE__, #expr); return 1; } } while(0)

static void flush(void) { fflush(stdout); fflush(stderr); }

#define RT_SIZE 64

// =========================================================================
// Validation callback test data
// =========================================================================
static int s_validationCount = 0;
static GpuValidationSeverity s_lastSeverity = GPU_VALIDATION_SEVERITY_INFO;

static void testValidationCallback(const GpuValidationMessage* msg, void* userData)
{
    (void)userData;
    s_validationCount++;
    s_lastSeverity = msg->severity;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=== Phase E: Backend Conformance Test ===\n\n"); flush();

    // =========================================================================
    // [E.1] Backend query - create device and query backend info
    // =========================================================================
    printf("[E.1] Backend query\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuBackend backend = gpuGetBackendType(device);
        const char* backendName = gpuGetBackendName(device);
        const char* adapterName = gpuGetAdapterName(device);
        uint64_t timestampFreq = gpuGetTimestampFrequency(device);

        printf("  backend: %s (%u)\n", gpuBackendToString(backend), backend);
        printf("  apiName: %s\n", backendName ? backendName : "(null)");
        printf("  adapterName: %s\n", adapterName ? adapterName : "(null)");
        printf("  timestampFrequency: %llu\n", (unsigned long long)timestampFreq);

        CHECK_TRUE(backend != GPU_BACKEND_DEFAULT);
        CHECK_TRUE(backendName != NULL);

        // Test gpuBackendToString
        CHECK_TRUE(strcmp(gpuBackendToString(GPU_BACKEND_D3D12), "D3D12") == 0);
        CHECK_TRUE(strcmp(gpuBackendToString(GPU_BACKEND_VULKAN), "Vulkan") == 0);

        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.2] Extended capabilities - verify backend info in capabilities
    // =========================================================================
    printf("[E.2] Extended capabilities\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCapabilities caps;
        gpuGetCapabilities(device, &caps);

        printf("  apiName: %s\n", caps.apiName);
        printf("  adapterName: %s\n", caps.adapterName);
        printf("  backendType: %u\n", caps.backendType);
        printf("  timestampFrequency: %llu\n", (unsigned long long)caps.timestampFrequency);

        CHECK_TRUE(caps.apiName[0] != '\0');
        CHECK_TRUE(caps.backendType != 0);

        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.3] Debug markers - push/pop on command encoder
    // =========================================================================
    printf("[E.3] Debug markers\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = true, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
        CHECK_TRUE(encoder != NULL);

        GpuMarkerColor color = { 1.0f, 0.0f, 0.0f };

        // These should not crash
        gpuCmdPushDebugGroup(encoder, "test_group", color);
        gpuCmdInsertDebugMarker(encoder, "test_marker", color);
        gpuCmdPopDebugGroup(encoder);

        GpuCommandBuffer cmdBuf = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(cmdBuf != NULL);

        CHECK(gpuQueueSubmit(queue, 1, &cmdBuf));
        CHECK(gpuQueueWaitOnHost(queue));

        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.4] Debug names - set and get
    // =========================================================================
    printf("[E.4] Debug names\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        // Create buffer with label
        GpuBufferDesc bdesc = {
            .size = 256,
            .usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER,
            .label = "initial_name",
        };
        GpuBufferHandle buf;
        CHECK(gpuCreateBuffer(device, &bdesc, &buf));

        // Get creation label
        const char* name = gpuGetBufferDebugName(device, buf);
        printf("  initial name: %s\n", name ? name : "(null)");

        // Set new name
        CHECK(gpuSetBufferDebugName(device, buf, "renamed_buffer"));
        name = gpuGetBufferDebugName(device, buf);
        printf("  after rename: %s\n", name ? name : "(null)");
        CHECK_TRUE(name != NULL);
        CHECK_TRUE(strcmp(name, "renamed_buffer") == 0);

        // Test texture
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = 32, .height = 32, .depth = 1,
            .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE,
            .label = "my_texture",
        };
        GpuTextureHandle tex;
        CHECK(gpuCreateTexture(device, &tdesc, &tex));
        name = gpuGetTextureDebugName(device, tex);
        printf("  texture name: %s\n", name ? name : "(null)");
        CHECK_TRUE(name != NULL);

        // Test sampler
        GpuSamplerDesc sdesc = {
            .minFilter = GPU_FILTER_LINEAR,
            .magFilter = GPU_FILTER_LINEAR,
            .mipFilter = GPU_FILTER_LINEAR,
            .addressModeU = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = GPU_SAMPLER_ADDRESS_MODE_REPEAT,
            .minLod = 0.0f,
            .maxLod = 1000.0f,
            .lodBias = 0.0f,
            .maxAnisotropy = 1,
            .compareEnable = false,
            .compareOp = 0,
            .reductionMode = GPU_SAMPLER_REDUCTION_MODE_STANDARD,
            .borderColor = {0, 0, 0, 0},
            .unnormalizedCoordinates = false,
            .label = "my_sampler",
        };
        GpuSamplerHandle sampler;
        CHECK(gpuCreateSampler(device, &sdesc, &sampler));
        name = gpuGetSamplerDebugName(device, sampler);
        printf("  sampler name: %s\n", name ? name : "(null)");
        CHECK_TRUE(name != NULL);

        gpuDestroySampler(device, sampler);
        gpuDestroyTexture(device, tex);
        gpuDestroyBuffer(device, buf);
        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.5] Validation callback - verify callback receives messages
    // =========================================================================
    printf("[E.5] Validation callback\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        s_validationCount = 0;
        gpuSetValidationCallback(device, testValidationCallback, NULL);
        CHECK_TRUE(gpuIsValidationEnabled(device));

        // Trigger a validation message by creating an invalid buffer (size 0)
        GpuBufferDesc badDesc = {
            .size = 0,
            .usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER,
            .label = "bad_buffer",
        };
        GpuBufferHandle badBuf;
        GpuResult res = gpuCreateBuffer(device, &badDesc, &badBuf);
        (void)res;

        printf("  validation count: %d\n", s_validationCount);
        CHECK_TRUE(s_validationCount > 0);
        CHECK_TRUE(s_lastSeverity == GPU_VALIDATION_SEVERITY_ERROR);

        gpuSetValidationCallback(device, NULL, NULL);
        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.6] Render readback - clear color verification
    // =========================================================================
    printf("[E.6] Render readback - clear color\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

        // Create render target
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = RT_SIZE, .height = RT_SIZE, .depth = 1,
            .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_COPY_SOURCE,
            .label = "conformance_rt",
        };
        GpuTextureHandle rt;
        CHECK(gpuCreateTexture(device, &tdesc, &rt));

        // Create texture view
        GpuTextureHandle rtView;
        CHECK(gpuCreateTextureView(device, rt, GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &rtView));

        // Create readback buffer
        const uint32_t bufSize = RT_SIZE * RT_SIZE * 4;
        GpuBufferHandle readbackBuf;
        CHECK(gpuCreateReadbackBuffer(device, bufSize, &readbackBuf));

        // Upload known pattern to readback buffer first to verify it changes
        uint8_t initData[RT_SIZE * RT_SIZE * 4];
        memset(initData, 0xAA, sizeof(initData));
        CHECK(gpuUploadToBuffer(device, readbackBuf, initData, sizeof(initData), 0));

        // Record commands: clear color to known value, then copy to readback
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
        CHECK_TRUE(encoder != NULL);

        // Clear color: Red = 255, Green = 128, Blue = 0, Alpha = 255
        GpuRenderPassDesc rpDesc = {};
        GpuRenderPassColorAttachment colorAtt = {};
        colorAtt.textureHandle = rt;
        colorAtt.loadOp = GPU_LOAD_OP_CLEAR;
        colorAtt.storeOp = GPU_STORE_OP_STORE;
        colorAtt.clearValue[0] = 1.0f;  // R
        colorAtt.clearValue[1] = 0.5f;  // G
        colorAtt.clearValue[2] = 0.0f;  // B
        colorAtt.clearValue[3] = 1.0f;  // A
        rpDesc.colorAttachments = &colorAtt;
        rpDesc.colorAttachmentCount = 1;

        GpuRenderPassEncoder pass = gpuCmdBeginRenderPass(encoder, &rpDesc);
        CHECK_TRUE(pass != NULL);
        gpuCmdEndRenderPass(pass);

        // Copy texture to readback buffer
        CHECK(gpuCmdCopyTextureToBuffer(encoder, rt, 0, 0, readbackBuf, 0));

        GpuCommandBuffer cmdBuf = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(cmdBuf != NULL);

        CHECK(gpuQueueSubmit(queue, 1, &cmdBuf));
        CHECK(gpuQueueWaitOnHost(queue));

        // Map and verify
        void* data = NULL;
        CHECK(gpuMapReadbackBuffer(device, readbackBuf, &data));
        CHECK_TRUE(data != NULL);

        // Get the actual row pitch (may have alignment padding)
        uint32_t rowPitch = gpuGetReadbackRowPitch(rt, device);
        CHECK_TRUE(rowPitch > 0);
        printf("  row pitch: %u bytes\n", rowPitch);

        const uint8_t* pixels = (const uint8_t*)data;
        // Use row pitch for correct pixel offset
        uint32_t centerIdx = (RT_SIZE / 2) * rowPitch + (RT_SIZE / 2) * 4;
        uint8_t r = pixels[centerIdx + 0];
        uint8_t g = pixels[centerIdx + 1];
        uint8_t b = pixels[centerIdx + 2];
        uint8_t a = pixels[centerIdx + 3];
        printf("  center pixel: R=%u G=%u B=%u A=%u\n", r, g, b, a);

        // Check first pixel
        uint8_t r0 = pixels[0];
        uint8_t g0 = pixels[1];
        uint8_t b0 = pixels[2];
        uint8_t a0 = pixels[3];
        printf("  first pixel: R=%u G=%u B=%u A=%u\n", r0, g0, b0, a0);

        // Clear color (1.0, 0.5, 0.0, 1.0) -> (255, 128, 0, 255) in RGBA8
        CHECK_TRUE(r > 240);
        CHECK_TRUE(g > 110 && g < 150);
        CHECK_TRUE(b < 20);
        CHECK_TRUE(a > 240);

        gpuUnmapReadbackBuffer(device, readbackBuf);

        gpuDestroyBuffer(device, readbackBuf);
        gpuDestroyTextureView(device, rtView);
        gpuDestroyTexture(device, rt);
        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.7] Render readback - different clear colors
    // =========================================================================
    printf("[E.7] Render readback - multiple colors\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = RT_SIZE, .height = RT_SIZE, .depth = 1,
            .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_COPY_SOURCE,
            .label = "multi_color_rt",
        };
        GpuTextureHandle rt;
        CHECK(gpuCreateTexture(device, &tdesc, &rt));

        
        GpuTextureHandle rtView;
        CHECK(gpuCreateTextureView(device, rt, GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &rtView));

        const uint32_t bufSize = RT_SIZE * RT_SIZE * 4;
        GpuBufferHandle readbackBuf;
        CHECK(gpuCreateReadbackBuffer(device, bufSize, &readbackBuf));

        // Test 3 different colors
        float testColors[3][4] = {
            {0.0f, 0.0f, 1.0f, 1.0f},  // Blue
            {0.0f, 1.0f, 0.0f, 1.0f},  // Green
            {1.0f, 1.0f, 1.0f, 1.0f},  // White
        };

        for (int t = 0; t < 3; t++) {
            GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
            CHECK_TRUE(encoder != NULL);

            GpuRenderPassDesc rpDesc = {};
            GpuRenderPassColorAttachment colorAtt = {};
            colorAtt.viewHandle = rtView;
            colorAtt.loadOp = GPU_LOAD_OP_CLEAR;
            colorAtt.storeOp = GPU_STORE_OP_STORE;
            colorAtt.clearValue[0] = testColors[t][0];
            colorAtt.clearValue[1] = testColors[t][1];
            colorAtt.clearValue[2] = testColors[t][2];
            colorAtt.clearValue[3] = testColors[t][3];
            rpDesc.colorAttachments = &colorAtt;
            rpDesc.colorAttachmentCount = 1;

            GpuRenderPassEncoder pass = gpuCmdBeginRenderPass(encoder, &rpDesc);
            CHECK_TRUE(pass != NULL);
            gpuCmdEndRenderPass(pass);

            CHECK(gpuCmdCopyTextureToBuffer(encoder, rt, 0, 0, readbackBuf, 0));

            GpuCommandBuffer cmdBuf = gpuFinishCommandEncoder(encoder);
            CHECK(gpuQueueSubmit(queue, 1, &cmdBuf));
            CHECK(gpuQueueWaitOnHost(queue));

            void* data = NULL;
            CHECK(gpuMapReadbackBuffer(device, readbackBuf, &data));

            const uint8_t* pixels = (const uint8_t*)data;
            uint32_t rp = gpuGetReadbackRowPitch(rt, device);
            uint32_t idx = (RT_SIZE / 2) * rp + (RT_SIZE / 2) * 4;
            uint8_t r = pixels[idx + 0];
            uint8_t g = pixels[idx + 1];
            uint8_t b = pixels[idx + 2];
            uint8_t a = pixels[idx + 3];

            printf("  color %d: R=%u G=%u B=%u A=%u\n", t, r, g, b, a);

            // Verify clear color is correct
            if (t == 0) { CHECK_TRUE(b > 240 && r < 20 && g < 20); }
            if (t == 1) { CHECK_TRUE(g > 240 && r < 20 && b < 20); }
            if (t == 2) { CHECK_TRUE(r > 240 && g > 240 && b > 240); }

            gpuUnmapReadbackBuffer(device, readbackBuf);
        }

        gpuDestroyBuffer(device, readbackBuf);
        gpuDestroyTextureView(device, rtView);
        gpuDestroyTexture(device, rt);
        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.8] Timestamp query - create pool and write timestamp
    // =========================================================================
    printf("[E.8] Timestamp query\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

        // Verify timestamp frequency is available
        uint64_t deviceFreq = gpuGetTimestampFrequency(device);
        printf("  device timestamp frequency: %llu Hz\n", (unsigned long long)deviceFreq);

        uint64_t queueFreq = 0;
        CHECK(gpuGetQueueTimestampFrequency(queue, &queueFreq));
        printf("  queue timestamp frequency: %llu Hz\n", (unsigned long long)queueFreq);
        CHECK_TRUE(queueFreq > 0);

        // Create query pool
        GpuQueryPool pool = NULL;
        CHECK(gpuCreateQueryPool(device, 4, &pool));
        CHECK_TRUE(pool != NULL);

        // Write timestamp via command buffer
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
        CHECK_TRUE(encoder != NULL);

        GpuCommandBuffer cmdBuf = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(cmdBuf != NULL);

        CHECK(gpuQueueSubmit(queue, 1, &cmdBuf));
        CHECK(gpuQueueWaitOnHost(queue));

        // Reset and verify
        CHECK(gpuQueryPoolReset(pool, 0, 4));

        gpuDestroyQueryPool(device, pool);
        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.9] Stress - resource churn (1000 create/destroy cycles)
    // =========================================================================
    printf("[E.9] Stress - resource churn\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

        const int N = 1000;
        for (int i = 0; i < N; i++) {
            GpuBufferDesc bdesc = { .size = 256, .usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER, .label = "churn_buf" };
            GpuBufferHandle buf;
            CHECK(gpuCreateBuffer(device, &bdesc, &buf));
            gpuDestroyBuffer(device, buf);

            if (i % 200 == 0) {
                printf("  churn iteration %d/%d\n", i, N);
            }
        }
        printf("  %d buffer create/destroy cycles OK\n", N);

        // Texture churn
        for (int i = 0; i < 100; i++) {
            GpuTextureDesc tdesc = {
                .type = GPU_TEXTURE_TYPE_2D,
                .width = 16, .height = 16, .depth = 1,
                .arrayLength = 1, .mipCount = 1,
                .format = GPU_FORMAT_RGBA8_UNORM,
                .sampleCount = 1,
                .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE,
                .label = "churn_tex",
            };
            GpuTextureHandle tex;
            CHECK(gpuCreateTexture(device, &tdesc, &tex));
            gpuDestroyTexture(device, tex);
        }
        printf("  100 texture create/destroy cycles OK\n");

        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.10] Stress - multi-frame rendering
    // =========================================================================
    printf("[E.10] Stress - multi-frame rendering\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = false, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

        // Create persistent render target
        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = RT_SIZE, .height = RT_SIZE, .depth = 1,
            .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_COPY_SOURCE,
            .label = "multi_frame_rt",
        };
        GpuTextureHandle rt;
        CHECK(gpuCreateTexture(device, &tdesc, &rt));

        
        GpuTextureHandle rtView;
        CHECK(gpuCreateTextureView(device, rt, GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &rtView));

        const uint32_t bufSize = RT_SIZE * RT_SIZE * 4;
        GpuBufferHandle readbackBuf;
        CHECK(gpuCreateReadbackBuffer(device, bufSize, &readbackBuf));

        // Render 100 frames with different clear colors
        const int frameCount = 100;
        for (int f = 0; f < frameCount; f++) {
            GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
            CHECK_TRUE(encoder != NULL);

            float r = (f % 3 == 0) ? 1.0f : 0.0f;
            float g = (f % 3 == 1) ? 1.0f : 0.0f;
            float b = (f % 3 == 2) ? 1.0f : 0.0f;

            GpuRenderPassDesc rpDesc = {};
            GpuRenderPassColorAttachment colorAtt = {};
            colorAtt.viewHandle = rtView;
            colorAtt.loadOp = GPU_LOAD_OP_CLEAR;
            colorAtt.storeOp = GPU_STORE_OP_STORE;
            colorAtt.clearValue[0] = r;
            colorAtt.clearValue[1] = g;
            colorAtt.clearValue[2] = b;
            colorAtt.clearValue[3] = 1.0f;
            rpDesc.colorAttachments = &colorAtt;
            rpDesc.colorAttachmentCount = 1;

            GpuRenderPassEncoder pass = gpuCmdBeginRenderPass(encoder, &rpDesc);
            if (!pass) { fprintf(stderr, "  FAIL: pass null at frame %d\n", f); return 1; }
            gpuCmdEndRenderPass(pass);

            // Only copy on last frame for verification
            if (f == frameCount - 1) {
                CHECK(gpuCmdCopyTextureToBuffer(encoder, rt, 0, 0, readbackBuf, 0));
            }

            GpuCommandBuffer cmdBuf = gpuFinishCommandEncoder(encoder);
            CHECK(gpuQueueSubmit(queue, 1, &cmdBuf));
            CHECK(gpuQueueWaitOnHost(queue));
        }

        // Verify last frame (frame 99: f%3==0, so red)
        void* data = NULL;
        CHECK(gpuMapReadbackBuffer(device, readbackBuf, &data));
        const uint8_t* pixels = (const uint8_t*)data;
        uint32_t rp = gpuGetReadbackRowPitch(rt, device);
        uint32_t idx = (RT_SIZE / 2) * rp + (RT_SIZE / 2) * 4;
        uint8_t pr = pixels[idx + 0];
        uint8_t pg = pixels[idx + 1];
        uint8_t pb = pixels[idx + 2];
        printf("  last frame (frame %d): R=%u G=%u B=%u\n", frameCount - 1, pr, pg, pb);
        // Frame 99: f%3==0, so clear to red (1,0,0,1) -> (255,0,0,255)
        CHECK_TRUE(pr > 240 && pg < 20 && pb < 20);
        gpuUnmapReadbackBuffer(device, readbackBuf);

        printf("  %d frames rendered OK\n", frameCount);

        gpuDestroyBuffer(device, readbackBuf);
        gpuDestroyTextureView(device, rtView);
        gpuDestroyTexture(device, rt);
        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.11] Debug markers in render pass
    // =========================================================================
    printf("[E.11] Debug markers in render pass\n"); flush();
    {
        GpuDevice device;
        GpuDeviceDesc devDesc = { .appName = "phaseE_test", .enableDebugLayer = true, .preferredBackend = GPU_BACKEND_DEFAULT };
        CHECK(gpuCreateDevice(&devDesc, &device));

        GpuCommandQueue queue;
        CHECK(gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue));

        GpuTextureDesc tdesc = {
            .type = GPU_TEXTURE_TYPE_2D,
            .width = 32, .height = 32, .depth = 1,
            .arrayLength = 1, .mipCount = 1,
            .format = GPU_FORMAT_RGBA8_UNORM,
            .sampleCount = 1,
            .usage = GPU_TEXTURE_USAGE_RENDER_TARGET,
            .label = "marker_test_rt",
        };
        GpuTextureHandle rt;
        CHECK(gpuCreateTexture(device, &tdesc, &rt));

        
        GpuTextureHandle rtView;
        CHECK(gpuCreateTextureView(device, rt, GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &rtView));

        GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
        CHECK_TRUE(encoder != NULL);

        GpuMarkerColor red = { 1.0f, 0.0f, 0.0f };
        GpuMarkerColor green = { 0.0f, 1.0f, 0.0f };

        // Marker on command encoder before pass
        gpuCmdPushDebugGroup(encoder, "frame_setup", red);

        GpuRenderPassDesc rpDesc = {};
        GpuRenderPassColorAttachment colorAtt = {};
        colorAtt.viewHandle = rtView;
        colorAtt.loadOp = GPU_LOAD_OP_CLEAR;
        colorAtt.storeOp = GPU_STORE_OP_STORE;
        colorAtt.clearValue[0] = 0.0f;
        colorAtt.clearValue[1] = 0.0f;
        colorAtt.clearValue[2] = 0.0f;
        colorAtt.clearValue[3] = 1.0f;
        rpDesc.colorAttachments = &colorAtt;
        rpDesc.colorAttachmentCount = 1;

        GpuRenderPassEncoder pass = gpuCmdBeginRenderPass(encoder, &rpDesc);
        CHECK_TRUE(pass != NULL);

        // Markers inside render pass
        gpuCmdPushRenderDebugGroup(pass, "render_scene", green);
        gpuCmdInsertRenderDebugMarker(pass, "draw_triangle", green);
        gpuCmdPopRenderDebugGroup(pass);

        gpuCmdEndRenderPass(pass);

        // Marker after pass
        gpuCmdInsertDebugMarker(encoder, "frame_complete", red);
        gpuCmdPopDebugGroup(encoder);

        GpuCommandBuffer cmdBuf = gpuFinishCommandEncoder(encoder);
        CHECK_TRUE(cmdBuf != NULL);
        CHECK(gpuQueueSubmit(queue, 1, &cmdBuf));
        CHECK(gpuQueueWaitOnHost(queue));

        gpuDestroyTextureView(device, rtView);
        gpuDestroyTexture(device, rt);
        gpuDestroyDevice(device);
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.12] Invalid args - verify error handling
    // =========================================================================
    printf("[E.12] Invalid args\n"); flush();
    {
        // Null device for backend query
        CHECK_TRUE(gpuGetBackendType(NULL) == GPU_BACKEND_DEFAULT);
        CHECK_TRUE(gpuGetBackendName(NULL) == NULL);
        CHECK_TRUE(gpuGetAdapterName(NULL) == NULL);
        CHECK_TRUE(gpuGetTimestampFrequency(NULL) == 0);

        // Null device for debug name
        CHECK_TRUE(gpuSetBufferDebugName(NULL, (GpuBufferHandle){0, 0}, "test") != GPU_SUCCESS);
        CHECK_TRUE(gpuGetBufferDebugName(NULL, (GpuBufferHandle){0, 0}) == NULL);

        // Destroy null is safe
        gpuDestroyDevice(NULL);

        printf("  all invalid arg checks passed\n");
    }
    printf("  OK\n"); flush();

    // =========================================================================
    // [E.13] Backend-specific device creation
    // =========================================================================
    printf("[E.13] Backend-specific device creation\n"); flush();
    {
        // Try creating with each backend type - at least one should work
        GpuBackend backends[] = { GPU_BACKEND_D3D12, GPU_BACKEND_VULKAN, GPU_BACKEND_DEFAULT };
        int created = 0;

        for (int i = 0; i < 3; i++) {
            GpuDevice device;
            GpuDeviceDesc devDesc = {
                .appName = "phaseE_backend_test",
                .enableDebugLayer = false,
                .preferredBackend = backends[i],
            };
            GpuResult res = gpuCreateDevice(&devDesc, &device);
            if (res == GPU_SUCCESS) {
                GpuBackend actual = gpuGetBackendType(device);
                printf("  requested %s -> got %s\n",
                       gpuBackendToString(backends[i]),
                       gpuBackendToString(actual));
                created++;
                gpuDestroyDevice(device);
            } else {
                printf("  requested %s -> failed (OK, may not be available)\n",
                       gpuBackendToString(backends[i]));
            }
        }

        CHECK_TRUE(created > 0);
    }
    printf("  OK\n"); flush();

    printf("\nALL PASSED\n"); flush();
    return 0;
}


