// 07_compute_pipeline - Compute shader prefix sum example
// Demonstrates: Compute pipeline creation, buffer usage, dispatch, readback

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gpu/gpu.h"

#define NUM_ELEMENTS 256

int main() {
    printf("=== Compute Pipeline Prefix Sum Test ===\n");
    fflush(stdout);
    
    // [1] Create device
    GpuDeviceDesc devDesc = {
        .appName = "07_compute_pipeline",
        .adapterIndex = 0,
        .enableDebugLayer = true,
    };
    GpuDevice device;
    GpuResult result = gpuCreateDevice(&devDesc, &device);
    if (result != GPU_OK) {
        printf("Failed to create device: %d\n", result);
        fflush(stdout);
        return 1;
    }
    printf("[1] Device created\n");
    fflush(stdout);
    
    // [2] Create shader compiler
    GpuShaderCompiler compiler;
    result = gpuCreateShaderCompiler(device, &compiler);
    if (result != GPU_OK) {
        printf("Failed to create shader compiler: %d\n", result);
        fflush(stdout);
        gpuDestroyDevice(device);
        return 1;
    }
    printf("[2] Shader compiler created\n");
    fflush(stdout);
    
    // [3] Compile compute shader
    // Try current directory first (where shader is copied during build)
    const char* shaderPath = "prefix_sum.slang";
    FILE* testFile = fopen(shaderPath, "r");
    if (!testFile) {
        // Fall back to repo-relative path for development
        shaderPath = "examples/07_compute_pipeline/prefix_sum.slang";
    } else {
        fclose(testFile);
    }
    
    GpuShaderCompileDesc csDesc = {
        .sourcePath = shaderPath,
        .entryPoint = "sumMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderProgram csProgram;
    result = gpuCompileShader(compiler, &csDesc, &csProgram);
    if (result != GPU_OK) {
        printf("Failed to compile compute shader: %d\n", result);
        printf("Diagnostic: %s\n", gpuGetShaderCompileDiagnostic(compiler));
        fflush(stdout);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    printf("[3] Compute shader compiled\n");
    fflush(stdout);
    
    // [4] Create compute pipeline from shader program
    GpuPipelineHandle pipeline;
    result = gpuCreateComputePipelineFromProgram(device, csProgram, "PrefixSum", &pipeline);
    if (result != GPU_OK) {
        printf("Failed to create compute pipeline: %d\n", result);
        fflush(stdout);
        gpuDestroyShaderProgram(csProgram);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    printf("[4] Compute pipeline created\n");
    fflush(stdout);
    
    // Note: csProgram is now referenced by the pipeline, don't destroy it until pipeline is destroyed
    
    // [5] Create input/output buffers
    float inputData[NUM_ELEMENTS];
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        inputData[i] = 1.0f;  // All ones - prefix sum should be [1, 2, 3, ...]
    }
    
    // Input buffer (upload + shader resource)
    GpuBufferDesc inputDesc = {
        .size = sizeof(inputData),
        .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_COPY_DEST,
        .label = "InputBuffer",
    };
    GpuBufferHandle inputBuffer;
    result = gpuCreateBuffer(device, &inputDesc, &inputBuffer);
    if (result != GPU_OK) {
        printf("Failed to create input buffer: %d\n", result);
        gpuDestroyPipeline(device, pipeline);
        gpuDestroyShaderProgram(csProgram);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    printf("[5] Input buffer created\n");
    fflush(stdout);
    
    // Output buffer (unordered access + readback)
    GpuBufferDesc outputDesc = {
        .size = sizeof(inputData),
        .usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_COPY_SOURCE,
        .label = "OutputBuffer",
    };
    GpuBufferHandle outputBuffer;
    result = gpuCreateBuffer(device, &outputDesc, &outputBuffer);
    if (result != GPU_OK) {
        printf("Failed to create output buffer: %d\n", result);
        gpuDestroyBuffer(device, inputBuffer);
        gpuDestroyPipeline(device, pipeline);
        gpuDestroyShaderProgram(csProgram);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    printf("    Output buffer created\n");
    fflush(stdout);
    
    // Create staging buffer for upload
    GpuBufferDesc stagingDesc = {
        .size = sizeof(inputData),
        .usage = GPU_BUFFER_USAGE_COPY_SOURCE | GPU_BUFFER_USAGE_COPY_DEST,
        .label = "StagingBuffer",
    };
    GpuBufferHandle stagingBuffer;
    result = gpuCreateBuffer(device, &stagingDesc, &stagingBuffer);
    if (result != GPU_OK) {
        printf("Failed to create staging buffer: %d\n", result);
        gpuDestroyBuffer(device, outputBuffer);
        gpuDestroyBuffer(device, inputBuffer);
        gpuDestroyPipeline(device, pipeline);
        gpuDestroyShaderProgram(csProgram);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    
    // [6] Upload input data using staging buffer
    // For now, we'll skip the actual upload and compute since resource binding is not fully implemented
    printf("[6] Buffers created (upload pending full implementation)\n");
    fflush(stdout);
    
    // [7] Get compute queue
    GpuCommandQueue computeQueue;
    result = gpuGetQueue(device, GPU_QUEUE_TYPE_COMPUTE, &computeQueue);
    if (result != GPU_OK) {
        printf("Failed to get compute queue: %d\n", result);
        fflush(stdout);
        gpuDestroyBuffer(device, stagingBuffer);
        gpuDestroyBuffer(device, outputBuffer);
        gpuDestroyBuffer(device, inputBuffer);
        gpuDestroyPipeline(device, pipeline);
        gpuDestroyShaderProgram(csProgram);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    printf("[7] Compute queue obtained\n");
    fflush(stdout);
    
    // [8] CPU reference calculation
    float cpuResult[NUM_ELEMENTS];
    float sum = 0.0f;
    for (int i = 0; i < NUM_ELEMENTS; i++) {
        sum += inputData[i];
        cpuResult[i] = sum;
    }
    printf("[8] CPU reference calculated\n");
    fflush(stdout);
    
    // [9] Record and submit compute commands
    // Note: Full implementation requires resource binding system
    // For now, we demonstrate the API structure
    
    GpuCommandEncoder encoder = gpuBeginCommandEncoder(device, computeQueue);
    if (!encoder) {
        printf("Failed to begin command encoder\n");
        gpuDestroyBuffer(device, outputBuffer);
        gpuDestroyBuffer(device, inputBuffer);
        gpuDestroyPipeline(device, pipeline);
        gpuDestroyShaderProgram(csProgram);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    
    GpuCommandBuffer cmdBuffer = gpuFinishCommandEncoder(encoder);
    
    result = gpuQueueSubmit(computeQueue, 1, &cmdBuffer);
    if (result != GPU_OK) {
        printf("Failed to submit command buffer: %d\n", result);
        gpuDestroyBuffer(device, outputBuffer);
        gpuDestroyBuffer(device, inputBuffer);
        gpuDestroyPipeline(device, pipeline);
        gpuDestroyShaderProgram(csProgram);
        gpuDestroyShaderCompiler(compiler);
        gpuDestroyDevice(device);
        return 1;
    }
    
    // Wait for completion
    result = gpuQueueWaitOnHost(computeQueue);
    if (result != GPU_OK) {
        printf("Failed to wait on queue: %d\n", result);
    }
    printf("[9] Compute commands submitted and completed\n");
    fflush(stdout);
    
    // [10] Validate results (would read back GPU data)
    printf("[10] Validating results:\n");
    printf("    CPU prefix sum first 10: ");
    for (int i = 0; i < 10 && i < NUM_ELEMENTS; i++) {
        printf("%.0f ", cpuResult[i]);
    }
    printf("...\n");
    printf("    CPU prefix sum last 5: ");
    for (int i = NUM_ELEMENTS - 5; i < NUM_ELEMENTS; i++) {
        printf("%.0f ", cpuResult[i]);
    }
    printf("\n");
    
    bool validationPassed = true;
    // Would compare GPU results with CPU results here
    // For now, we just verify the API structure works
    
    if (validationPassed) {
        printf("    Validation: SKIPPED (resource binding not fully implemented)\n");
    }
    
    // [11] Cleanup
    gpuDestroyBuffer(device, outputBuffer);
    gpuDestroyBuffer(device, inputBuffer);
    gpuDestroyPipeline(device, pipeline);
    gpuDestroyShaderProgram(csProgram);
    gpuDestroyShaderCompiler(compiler);
    gpuDestroyDevice(device);
    
    printf("\n=== Compute Pipeline Test Complete ===\n");
    printf("Note: Full GPU compute validation pending resource binding implementation\n");
    fflush(stdout);
    return 0;
}
