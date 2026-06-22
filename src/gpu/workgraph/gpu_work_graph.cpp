#include "gpu/workgraph/gpu_work_graph.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/debug/gpu_validation.h"
#include <slang-rhi.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <map>
#include <mutex>
#include <string>
#include <vector>

extern rhi::IComputePipeline* gpuResolveComputePipeline(GpuDevice device, GpuPipelineHandle pipeline);

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

struct WorkGraphBackendData {
    GpuPipelineHandle computePipeline;
    GpuShaderProgram program;
    GpuShaderCompiler compiler;
    uint32_t dispatchX, dispatchY, dispatchZ;
    bool isInitialized;
    std::string entryPoint;
};

static std::map<uint64_t, WorkGraphBackendData> s_graphData;
static std::mutex s_graphMutex;
static uint64_t s_nextGraphId = 1;

static bool writeWorkGraphShaderToTempFile(const char* content, const char* suffix, std::string& outPath) {
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    outPath = std::string(tempDir) + "gpu_wg_" + suffix + ".slang";
    FILE* f = fopen(outPath.c_str(), "w");
    if (!f) return false;
    fputs(content, f);
    fclose(f);
    return true;
}

GpuResult gpuCreateWorkGraph(GpuDevice device, const GpuWorkGraphDesc* desc, GpuWorkGraph* outGraph)
{
    if (!device || !desc || !outGraph) return GPU_ERROR_INVALID_ARGS;

    GpuFeatureInfo fi;
    if (gpuGetFeatureInfo(device, GPU_FEATURE_WORK_GRAPH, &fi) != GPU_SUCCESS ||
        fi.support == GPU_FEATURE_SUPPORT_UNSUPPORTED) {
        GPU_FEATURE_GATE(device, GPU_FEATURE_WORK_GRAPH, "WorkGraph");
    }

    std::lock_guard<std::mutex> lock(s_graphMutex);

    uint64_t graphId = s_nextGraphId++;
    WorkGraphBackendData& data = s_graphData[graphId];

    data.isInitialized = false;
    data.computePipeline = {0, 0};
    data.program = nullptr;
    data.compiler = nullptr;

    if (desc->nodeCount == 0 || !desc->nodes) {
        *outGraph = (GpuWorkGraph)(uintptr_t)graphId;
        return GPU_SUCCESS;
    }

    GpuResult res = gpuCreateShaderCompiler(device, &data.compiler);
    if (res != GPU_SUCCESS) {
        s_graphData.erase(graphId);
        return res;
    }

    bool anyNodeWithData = false;
    for (uint32_t i = 0; i < desc->nodeCount; i++) {
        if (desc->nodes[i].nodeShader.data && desc->nodes[i].nodeShader.size > 0) {
            anyNodeWithData = true;

            std::string shaderSource((const char*)desc->nodes[i].nodeShader.data, (size_t)desc->nodes[i].nodeShader.size);
            std::string shaderPath;
            char suffix[32];
            snprintf(suffix, sizeof(suffix), "node%u", i);
            if (!writeWorkGraphShaderToTempFile(shaderSource.c_str(), suffix, shaderPath)) {
                gpuDestroyShaderCompiler(data.compiler);
                s_graphData.erase(graphId);
                return GPU_ERROR_INTERNAL;
            }

            GpuShaderCompileDesc compileDesc = {};
            compileDesc.sourcePath = shaderPath.c_str();
            compileDesc.entryPoint = desc->nodes[i].entryPoint ? desc->nodes[i].entryPoint : "main";
            compileDesc.target = GPU_SHADER_TARGET_SPIRV;

            GpuShaderProgram nodeProgram = nullptr;
            res = gpuCompileShader(data.compiler, &compileDesc, &nodeProgram);
            if (res == GPU_SUCCESS && nodeProgram) {
                GpuPipelineHandle nodePipe = {0, 0};
                GpuResult pipeRes = gpuCreateComputePipelineFromProgram(device, nodeProgram, desc->nodes[i].name, &nodePipe);
                if (pipeRes == GPU_SUCCESS) {
                    data.computePipeline = nodePipe;
                    data.program = nodeProgram;
                    data.isInitialized = true;
                    data.entryPoint = compileDesc.entryPoint;

                    data.dispatchX = 1;
                    data.dispatchY = 1;
                    data.dispatchZ = 1;
                }
                break;
            }
        }
    }

    (void)anyNodeWithData;
    *outGraph = (GpuWorkGraph)(uintptr_t)graphId;
    return GPU_SUCCESS;
}

GpuResult gpuWorkGraphInitialize(GpuWorkGraph graph, GpuCommandBuffer cmd)
{
    if (!graph || !cmd) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_graphMutex);
    uint64_t graphId = (uint64_t)(uintptr_t)graph;
    auto it = s_graphData.find(graphId);
    if (it == s_graphData.end()) return GPU_ERROR_INVALID_ARGS;

    return GPU_SUCCESS;
}

void gpuCmdDispatchWorkGraph(GpuCommandBuffer cmd, GpuWorkGraph graph, uint32_t recordCount)
{
    if (!cmd || !graph) return;

    std::lock_guard<std::mutex> lock(s_graphMutex);
    uint64_t graphId = (uint64_t)(uintptr_t)graph;
    auto it = s_graphData.find(graphId);
    if (it == s_graphData.end()) return;

    WorkGraphBackendData& data = it->second;
    if (!data.isInitialized) return;
    if (!cmd->rhiEncoder) return;

    if (!cmd->inComputePass) {
        cmd->computePassEncoder = cmd->rhiEncoder->beginComputePass();
        if (cmd->computePassEncoder) cmd->inComputePass = true;
    }

    if (cmd->inComputePass && cmd->computePassEncoder) {
        rhi::IComputePipeline* pipe = gpuResolveComputePipeline(cmd->device, data.computePipeline);
        if (pipe) {
            cmd->computePassEncoder->bindPipeline(pipe);

            uint32_t totalDispatch = recordCount > 0 ? recordCount : 1;
            uint32_t dx = (data.dispatchX * totalDispatch + 0) / 1;
            uint32_t dy = data.dispatchY;
            uint32_t dz = data.dispatchZ;
            cmd->computePassEncoder->dispatchCompute(dx, dy, dz);

            cmd->computePassEncoder->end();
            cmd->inComputePass = false;
            cmd->computePassEncoder = nullptr;
        }
    }
}

void gpuDestroyWorkGraph(GpuDevice device, GpuWorkGraph graph)
{
    if (!device || !graph) return;

    std::lock_guard<std::mutex> lock(s_graphMutex);
    uint64_t graphId = (uint64_t)(uintptr_t)graph;
    auto it = s_graphData.find(graphId);
    if (it == s_graphData.end()) return;

    WorkGraphBackendData& data = it->second;
    if (data.isInitialized) {
        gpuDestroyPipeline(device, data.computePipeline);
        if (data.program) gpuDestroyShaderProgram(data.program);
        if (data.compiler) gpuDestroyShaderCompiler(data.compiler);
    }
    s_graphData.erase(it);
}
