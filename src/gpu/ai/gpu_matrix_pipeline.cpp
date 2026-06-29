#include "gpu/ai/gpu_matrix_pipeline.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include <slang.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <map>
#include <mutex>
#include <string>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

extern rhi::IComputePipeline* gpuResolveComputePipeline(GpuDevice device, GpuPipelineHandle pipeline);

static const char* s_matmulShaderFormat = R"(
static const uint MATMUL_M = %u;
static const uint MATMUL_N = %u;
static const uint MATMUL_K = %u;

RWStructuredBuffer<float> bufferA;
RWStructuredBuffer<float> bufferB;
RWStructuredBuffer<float> bufferC;

[numthreads(16, 16, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint row = threadID.y;
    uint col = threadID.x;

    if (row >= MATMUL_M || col >= MATMUL_N)
        return;

    float sum = 0.0;
    for (uint k = 0; k < MATMUL_K; k++) {
        float a = bufferA[row * MATMUL_K + k];
        float b = bufferB[k * MATMUL_N + col];
        sum += a * b;
    }
    bufferC[row * MATMUL_N + col] = sum;
}
)";

struct MatmulPipelineEntry {
    GpuDevice device;
    GpuShaderProgram program;
    GpuPipelineHandle pipeline;
    GpuShaderCompiler compiler;
    uint32_t m, n, k;
    bool initialized;
};

static std::map<std::string, MatmulPipelineEntry> s_matmulCache;
static std::mutex s_matmulMutex;

static std::string matmulCacheKey(uint32_t m, uint32_t n, uint32_t k) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%u_%u_%u", m, n, k);
    return std::string(buf);
}

static bool writeShaderToTempFile(const char* content, std::string& outPath) {
    return gpuWriteTextTempFile("gpu_matmul.slang", content, outPath);
}

static std::string makeMatmulShaderSource(uint32_t m, uint32_t n, uint32_t k)
{
    int needed = snprintf(nullptr, 0, s_matmulShaderFormat, m, n, k);
    if (needed <= 0) return std::string();
    std::string source((size_t)needed, '\0');
    snprintf(source.data(), source.size() + 1, s_matmulShaderFormat, m, n, k);
    return source;
}

static bool setCursorBufferIfValid(rhi::ShaderCursor cursor, const char* name, rhi::IBuffer* buffer)
{
    rhi::ShaderCursor field = cursor[name];
    if (!field.isValid()) return false;
    return SLANG_SUCCEEDED(field.setBinding(rhi::Binding(buffer)));
}

GpuResult gpuCreateMatmulPipeline(GpuDevice device, const GpuMatmulDesc* desc, GpuPipelineHandle* outPipeline)
{
    if (!device || !desc || !outPipeline) return GPU_ERROR_INVALID_ARGS;
    if (desc->m == 0 || desc->n == 0 || desc->k == 0) return GPU_ERROR_INVALID_ARGS;
    if (desc->aFormat != GPU_TENSOR_FORMAT_F32 ||
        desc->bFormat != GPU_TENSOR_FORMAT_F32 ||
        desc->cFormat != GPU_TENSOR_FORMAT_F32) {
        return GPU_ERROR_NOT_SUPPORTED;
    }

    std::lock_guard<std::mutex> lock(s_matmulMutex);
    std::string key = matmulCacheKey(desc->m, desc->n, desc->k);

    auto it = s_matmulCache.find(key);
    if (it != s_matmulCache.end() && it->second.initialized) {
        *outPipeline = it->second.pipeline;
        return GPU_SUCCESS;
    }

    MatmulPipelineEntry& entry = s_matmulCache[key];
    entry.device = device;
    entry.m = desc->m;
    entry.n = desc->n;
    entry.k = desc->k;

    GpuResult res = gpuCreateShaderCompiler(device, &entry.compiler);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "gpuCreateMatmulPipeline: shader compiler creation failed %d\n", res);
        return res;
    }

    std::string shaderPath;
    std::string shaderSource = makeMatmulShaderSource(desc->m, desc->n, desc->k);
    if (shaderSource.empty() || !writeShaderToTempFile(shaderSource.c_str(), shaderPath)) {
        fprintf(stderr, "gpuCreateMatmulPipeline: failed to write temp shader\n");
        gpuDestroyShaderCompiler(entry.compiler);
        entry.compiler = NULL;
        return GPU_ERROR_INTERNAL;
    }

    GpuShaderCompileDesc compileDesc = {};
    compileDesc.sourcePath = shaderPath.c_str();
    compileDesc.entryPoint = "main";
    compileDesc.target = GPU_SHADER_TARGET_SPIRV;

    res = gpuCompileShader(entry.compiler, &compileDesc, &entry.program);
    if (res != GPU_SUCCESS) {
        const char* diag = gpuGetShaderCompileDiagnostic(entry.compiler);
        fprintf(stderr, "gpuCreateMatmulPipeline: shader compile failed %d (%s)\n", res, diag ? diag : "");
        gpuDestroyShaderCompiler(entry.compiler);
        entry.compiler = NULL;
        return res;
    }

    res = gpuCreateComputePipelineFromProgram(device, entry.program, "Matmul", &entry.pipeline);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "gpuCreateMatmulPipeline: pipeline creation failed %d\n", res);
        gpuDestroyShaderProgram(entry.program);
        entry.program = NULL;
        gpuDestroyShaderCompiler(entry.compiler);
        entry.compiler = NULL;
        return res;
    }

    entry.initialized = true;
    *outPipeline = entry.pipeline;
    return GPU_SUCCESS;
}

void gpuCmdMatmul(GpuCommandBuffer cmd, GpuPipelineHandle matmulPipeline, const GpuMatmulBindings* bindings)
{
    if (!cmd || !cmd->device || !matmulPipeline.index || !bindings) return;

    GpuDevice device = cmd->device;

    GpuBufferHandle bufferA = gpuGetTensorBuffer(device, bindings->a);
    GpuBufferHandle bufferB = gpuGetTensorBuffer(device, bindings->b);
    GpuBufferHandle bufferC = gpuGetTensorBuffer(device, bindings->c);

    if (!bufferA.index || !bufferB.index || !bufferC.index) {
        fprintf(stderr, "gpuCmdMatmul: Invalid tensor buffers\n");
        return;
    }

    std::lock_guard<std::mutex> lock(s_matmulMutex);
    for (auto& [key, entry] : s_matmulCache) {
        if (entry.pipeline.index == matmulPipeline.index &&
            entry.pipeline.generation == matmulPipeline.generation &&
            entry.initialized) {

            if (!cmd->rhiEncoder) return;

            if (!cmd->inComputePass) {
                cmd->computePassEncoder = cmd->rhiEncoder->beginComputePass();
                if (cmd->computePassEncoder) {
                    cmd->inComputePass = true;
                }
            }

            if (cmd->inComputePass && cmd->computePassEncoder) {
                rhi::IComputePipeline* pipe = gpuResolveComputePipeline(cmd->device, matmulPipeline);
                if (pipe) {
                    rhi::IBuffer* rhiA = device->bufferPool.resolve(bufferA.index, bufferA.generation);
                    rhi::IBuffer* rhiB = device->bufferPool.resolve(bufferB.index, bufferB.generation);
                    rhi::IBuffer* rhiC = device->bufferPool.resolve(bufferC.index, bufferC.generation);
                    if (!rhiA || !rhiB || !rhiC) return;

                    rhi::ComPtr<rhi::IShaderObject> rootObject = device->rhiDevice->createRootShaderObject(pipe);
                    if (!rootObject) return;

                    rhi::ShaderCursor rootCursor(rootObject);
                    bool boundA = setCursorBufferIfValid(rootCursor, "bufferA", rhiA);
                    bool boundB = setCursorBufferIfValid(rootCursor, "bufferB", rhiB);
                    bool boundC = setCursorBufferIfValid(rootCursor, "bufferC", rhiC);

                    rhi::IShaderObject* entryPointObject = rootObject->getEntryPoint(0);
                    if (entryPointObject) {
                        rhi::ShaderCursor entryCursor(entryPointObject);
                        if (!boundA) boundA = setCursorBufferIfValid(entryCursor, "bufferA", rhiA);
                        if (!boundB) boundB = setCursorBufferIfValid(entryCursor, "bufferB", rhiB);
                        if (!boundC) boundC = setCursorBufferIfValid(entryCursor, "bufferC", rhiC);
                    }

                    if (!boundA || !boundB || !boundC) {
                        fprintf(stderr, "gpuCmdMatmul: failed to bind shader parameters (A=%d B=%d C=%d)\n",
                                boundA ? 1 : 0, boundB ? 1 : 0, boundC ? 1 : 0);
                        return;
                    }

                    cmd->computePassEncoder->bindPipeline(pipe, rootObject);
                }

                uint32_t dispatchX = (entry.n + 15) / 16;
                uint32_t dispatchY = (entry.m + 15) / 16;
                cmd->computePassEncoder->dispatchCompute(dispatchX, dispatchY, 1);
            }
            return;
        }
    }
}
