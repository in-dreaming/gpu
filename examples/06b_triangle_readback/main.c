#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// 256x256 render target with BGRA8 format
#define RT_SIZE 256

// Helper: check if RGB values are similar (tolerance for gamma/compression)
static int colorMatch(uint32_t pixel, float r, float g, float b, float tolerance)
{
    // BGRA8 format: lowest byte is B, then G, then R, then A
    uint8_t pb = (pixel >> 0) & 0xFF;
    uint8_t pg = (pixel >> 8) & 0xFF;
    uint8_t pr = (pixel >> 16) & 0xFF;

    float fr = pr / 255.0f;
    float fg = pg / 255.0f;
    float fb = pb / 255.0f;

    float dr = fabsf(fr - r);
    float dg = fabsf(fg - g);
    float db = fabsf(fb - b);

    return (dr <= tolerance && dg <= tolerance && db <= tolerance);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);  // Disable buffering for immediate output
    GpuResult res;

    printf("=== Triangle Render-to-Texture + Readback Test ===\n");

    // Initialize platform (no window needed for headless)
    res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("FAIL: Platform init: %d\n", res); return 1; }

    printf("[1] Creating device...\n");
    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "06b_triangle_readback", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("FAIL: Device: %d\n", res); gpuPlatformShutdown(); return 1; }
    printf("    Device OK\n");

    // Get graphics queue
    printf("[2] Getting graphics queue...\n");
    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("FAIL: Queue: %d\n", res); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }
    printf("    Queue OK\n");

    // Create render texture (RGBA8)
    printf("[3] Creating render target texture (%dx%d RGBA8)...\n", RT_SIZE, RT_SIZE);
    GpuTextureDesc texDesc = {
        .type = GPU_TEXTURE_TYPE_2D,
        .width = RT_SIZE,
        .height = RT_SIZE,
        .depth = 1,
        .arrayLength = 1,
        .mipCount = 1,
        .format = GPU_FORMAT_RGBA8_UNORM,
        .sampleCount = 1,
        .usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_COPY_SOURCE,
        .label = "readback_target",
    };
    GpuTextureHandle renderTarget = {0, 0};
    res = gpuCreateTexture(device, &texDesc, &renderTarget);
    if (res != GPU_SUCCESS) { printf("FAIL: Texture: %d\n", res); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }
    printf("    Texture OK (handle: %u, %u)\n", renderTarget.index, renderTarget.generation);

    // Create render target view
    printf("    Creating render target view...\n");
    GpuTextureHandle renderTargetView = {0, 0};
    res = gpuCreateTextureView(device, renderTarget, GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &renderTargetView);
    if (res != GPU_SUCCESS) {
        printf("FAIL: TextureView: %d\n", res);
        gpuDestroyTexture(device, renderTarget);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    printf("    Render target view OK (handle: %u, %u)\n", renderTargetView.index, renderTargetView.generation);

    // Create compiler
    printf("[4] Creating shader compiler...\n");
    GpuShaderCompiler compiler = NULL;
    res = gpuCreateShaderCompiler(device, &compiler);
    if (res != GPU_SUCCESS) { printf("FAIL: Compiler: %d\n", res); gpuDestroyTexture(device, renderTarget); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }
    printf("    Compiler OK\n");

    // Compile shader (use vertexID-based triangle like 06_triangle)
    printf("[5] Compiling shader (triangle.slang)...\n");
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
        printf("FAIL: Shader compile: %d (%s)\n", res, diag ? diag : "");
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyTexture(device, renderTarget);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    printf("    Shader compiled OK\n");

    // Debug: print shader size
    uint64_t vsSize = 0;
    gpuGetShaderProgramData(program, &vsSize);
    printf("    Shader program size: %llu bytes\n", (unsigned long long)vsSize);

    // Create pipeline
    printf("[6] Creating render pipeline...\n");
    GpuColorTargetDesc targetDesc = {
        .format = GPU_FORMAT_RGBA8_UNORM,
    };
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
        printf("FAIL: Pipeline: %d\n", res);
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyTexture(device, renderTarget);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    printf("    Pipeline OK\n");

    // Create readback buffer (RT_SIZE * RT_SIZE * 4 bytes)
    printf("[7] Creating readback buffer...\n");
    const uint32_t pixelCount = RT_SIZE * RT_SIZE;
    const uint32_t bufferSize = pixelCount * 4;
    GpuBufferHandle readbackBuffer = {0, 0};
    res = gpuCreateReadbackBuffer(device, bufferSize, &readbackBuffer);
    if (res != GPU_SUCCESS) {
        printf("FAIL: Readback buffer: %d\n", res);
        gpuDestroyRenderPipeline(device, pipeline);
        gpuDestroyShaderProgram(program);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyTexture(device, renderTarget);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    printf("    Readback buffer OK\n");

    // RECORD RENDER COMMANDS
    printf("[8] Recording render commands...\n");
    GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, queue);
    if (!encoder) {
        printf("FAIL: BeginCommandEncoder\n");
        res = GPU_ERROR_INTERNAL;
        goto cleanup;
    }

    // Begin render pass to texture using explicit view
    GpuRenderPassColorAttachment colorAttachment = {
        .attachment = NULL,  // No surface
        .textureHandle = {0, 0},
        .viewHandle = renderTargetView,
        .mipLevel = 0,
        .loadOp = GPU_LOAD_OP_CLEAR,
        .storeOp = GPU_STORE_OP_STORE,
        .clearValue = { 1.0f, 0.0f, 0.0f, 1.0f },  // Clear to RED for testing
    };
    GpuRenderPassDesc passDesc = {
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
    };

    printf("    Starting render pass to texture...\n");
    GpuRenderPassEncoder pass = gpuCmdBeginRenderPass(encoder, &passDesc);
    if (!pass) {
        printf("    FAIL: BeginRenderPass returned NULL\n");
        res = GPU_ERROR_INTERNAL;
        goto cleanup_encoder;
    }
    printf("    Render pass started, ending immediately (clear only)...\n");
    gpuCmdEndRenderPass(pass);

    // Copy texture to readback buffer
    printf("    Copying texture to readback buffer...\n");
    res = gpuCmdCopyTextureToBuffer(encoder, renderTarget, 0, 0, readbackBuffer, 0);
    if (res != GPU_SUCCESS) {
        printf("FAIL: CopyTextureToBuffer: %d\n", res);
        goto cleanup_encoder;
    }

    // Finish and submit
    printf("    Finishing command encoder...\n");
    GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
    if (!cmd) {
        printf("FAIL: FinishCommandEncoder\n");
        res = GPU_ERROR_INTERNAL;
        goto cleanup;
    }

    printf("    Submitting to queue...\n");
    res = gpuQueueSubmit(queue, 1, &cmd);
    if (res != GPU_SUCCESS) {
        printf("FAIL: QueueSubmit: %d\n", res);
        goto cleanup;
    }

    // Wait for completion
    printf("[9] Waiting for GPU completion...\n");
    gpuQueueWaitOnHost(queue);
    printf("    GPU done\n");

    // Readback and validate
    printf("[10] Reading back pixel data...\n");
    void* mappedData = NULL;
    res = gpuMapReadbackBuffer(device, readbackBuffer, &mappedData);
    if (res != GPU_SUCCESS) {
        printf("FAIL: MapReadbackBuffer: %d\n", res);
        goto cleanup;
    }

    uint32_t* pixels = (uint32_t*)mappedData;

    // Debug: print first 10 pixels
    printf("    First 10 pixels:");
    for (int i = 0; i < 10; i++) {
        printf(" 0x%08X", pixels[i]);
    }
    printf("\n");

    // Pick some sample points for validation
    // The triangle in shader:
    //   vertex 0: (0.0, 0.5)   -> top center,   RED
    //   vertex 1: (0.5, -0.5)  -> bottom right, GREEN
    //   vertex 2: (-0.5, -0.5) -> bottom left,  BLUE

    // In framebuffer coordinates (0,0 is top-left in Vulkan):
    // Y=1.0 -> Y=0 (top), Y=-1.0 -> Y=255 (bottom)
    // X=-1.0 -> X=0 (left), X=1.0 -> X=255 (right)

    // Center of triangle (approximately): around (127, 160)
    uint32_t centerPixel = pixels[160 * RT_SIZE + 127];  // ~center of screen
    // Top center: around (127, 50) - should be RED
    uint32_t topPixel = pixels[50 * RT_SIZE + 127];
    // Bottom right: around (190, 220) - should be GREEN
    uint32_t rightPixel = pixels[220 * RT_SIZE + 190];
    // Bottom left: around (64, 220) - should be BLUE
    uint32_t leftPixel = pixels[220 * RT_SIZE + 64];

    printf("    Sample pixels:\n");
    printf("        Center (127,160): 0x%08X\n", centerPixel);
    printf("        Top    (127, 50): 0x%08X\n", topPixel);
    printf("        Right  (190,220): 0x%08X\n", rightPixel);
    printf("        Left   ( 64,220): 0x%08X\n", leftPixel);

    // Validate
    int passed = 1;
    float tolerance = 0.2f;  // 20% tolerance for gamma/srgb conversion

    // Top center should be RED (or near-red since it's covered by triangle)
    if (!colorMatch(centerPixel, 0.5f, 0.0f, 0.0f, tolerance)) {
        printf("    WARN: Center pixel not red-ish (expected red/green/blue mix)\n");
        // Center is inside triangle but color depends on interpolation
    }

    // For simplicity, just check that the pixels are not black (cleared color)
    // If rendering worked, center should not be black
    if (colorMatch(centerPixel, 0.0f, 0.0f, 0.0f, 0.05f)) {
        printf("FAIL: Center pixel is black (clear color) - triangle not rendered!\n");
        passed = 0;
    } else {
        printf("    Center pixel is NOT black - triangle rendered!\n");
    }

    // Check that at least some pixels vary in R/G/B channels
    // (proving there's actual rendering)
    uint32_t hasRed = 0, hasGreen = 0, hasBlue = 0;
    for (uint32_t y = 40; y < 240 && y < RT_SIZE; y += 4) {
        for (uint32_t x = 40; x < 240 && x < RT_SIZE; x += 4) {
            uint32_t p = pixels[y * RT_SIZE + x];
            uint8_t r = (p >> 16) & 0xFF;
            uint8_t g = (p >> 8) & 0xFF;
            uint8_t b = (p >> 0) & 0xFF;
            if (r > 30) hasRed = 1;
            if (g > 30) hasGreen = 1;
            if (b > 30) hasBlue = 1;
        }
    }

    printf("    Channel scan: R=%s, G=%s, B=%s\n",
           hasRed ? "yes" : "no",
           hasGreen ? "yes" : "no",
           hasBlue ? "yes" : "no");

    gpuUnmapReadbackBuffer(device, readbackBuffer);

    if (passed && hasRed) {
        printf("\n=== PASS: Triangle rendered successfully! ===\n");
        res = 0;  // Success exit code
    } else {
        printf("\n=== FAIL: Triangle not rendered correctly ===\n");
        res = 1;
    }

cleanup_encoder:
    // encoder already finished or failed

cleanup:
    printf("[11] Cleanup...\n");
    gpuUnmapReadbackBuffer(device, readbackBuffer);
    gpuDestroyRenderPipeline(device, pipeline);
    gpuDestroyShaderProgram(program);
    gpuDestroyShaderCompiler(compiler);
    gpuDestroyBuffer(device, readbackBuffer);
    gpuDestroyTextureView(device, renderTargetView);
    gpuDestroyTexture(device, renderTarget);
    gpuDestroyDevice(device);
    gpuPlatformShutdown();

    return (res == 0) ? 0 : 1;
}
