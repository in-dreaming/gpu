#include "gpu/pipeline/gpu_mesh_pipeline.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include <slang.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern void ensureRenderPass(GpuCommandBuffer_t* buf);

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

static bool writeShaderToTempFile(const char* content, const char* suffix, std::string& outPath) {
    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    outPath = std::string(tempDir) + "gpu_mesh_" + suffix + ".slang";
    FILE* f = fopen(outPath.c_str(), "w");
    if (!f) return false;
    fputs(content, f);
    fclose(f);
    return true;
}

GpuResult gpuCreateMeshPipeline(GpuDevice device, const GpuMeshPipelineDesc* desc, GpuPipelineHandle* outPipeline) {
    if (!device || !desc || !outPipeline) return GPU_ERROR_INVALID_ARGS;
    if (!desc->meshShader.data || desc->meshShader.size == 0) return GPU_ERROR_INVALID_ARGS;
    if (!desc->fragmentShader.data || desc->fragmentShader.size == 0) return GPU_ERROR_INVALID_ARGS;

    if (!device->rhiDevice->hasFeature(rhi::Feature::MeshShader)) {
        return GPU_ERROR_NOT_SUPPORTED;
    }

    rhi::ComPtr<slang::ISession> slangSession;
    if (SLANG_FAILED(device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    bool hasAmplification = desc->amplificationShader.data != nullptr && desc->amplificationShader.size > 0;

    std::string meshShaderSource((const char*)desc->meshShader.data, (size_t)desc->meshShader.size);
    std::string fragShaderSource((const char*)desc->fragmentShader.data, (size_t)desc->fragmentShader.size);
    std::string ampShaderSource;
    if (hasAmplification) {
        ampShaderSource = std::string((const char*)desc->amplificationShader.data, (size_t)desc->amplificationShader.size);
    }

    std::string meshPath, fragPath, ampPath;
    if (!writeShaderToTempFile(meshShaderSource.c_str(), "mesh", meshPath)) return GPU_ERROR_INTERNAL;
    if (!writeShaderToTempFile(fragShaderSource.c_str(), "frag", fragPath)) return GPU_ERROR_INTERNAL;
    if (hasAmplification) {
        if (!writeShaderToTempFile(ampShaderSource.c_str(), "amp", ampPath)) return GPU_ERROR_INTERNAL;
    }

    rhi::ComPtr<slang::IModule> meshModule;
    slang::IBlob* diagBlob = nullptr;
    meshModule = slangSession->loadModule(meshPath.c_str(), &diagBlob);
    if (diagBlob) diagBlob->release();
    if (!meshModule) return GPU_ERROR_INTERNAL;

    rhi::ComPtr<slang::IModule> fragModule;
    fragModule = slangSession->loadModule(fragPath.c_str(), &diagBlob);
    if (diagBlob) diagBlob->release();
    if (!fragModule) return GPU_ERROR_INTERNAL;

    rhi::ComPtr<slang::IEntryPoint> meshEntry;
    if (SLANG_FAILED(meshModule->findEntryPointByName("meshMain", meshEntry.writeRef()))) {
        if (SLANG_FAILED(meshModule->findEntryPointByName("main", meshEntry.writeRef()))) {
            return GPU_ERROR_INVALID_ARGS;
        }
    }

    rhi::ComPtr<slang::IEntryPoint> fragEntry;
    if (SLANG_FAILED(fragModule->findEntryPointByName("fragmentMain", fragEntry.writeRef()))) {
        if (SLANG_FAILED(fragModule->findEntryPointByName("main", fragEntry.writeRef()))) {
            return GPU_ERROR_INVALID_ARGS;
        }
    }

    rhi::ComPtr<slang::IEntryPoint> ampEntry;
    rhi::ComPtr<slang::IModule> ampModule;

    if (hasAmplification) {
        ampModule = slangSession->loadModule(ampPath.c_str(), &diagBlob);
        if (diagBlob) diagBlob->release();
        if (!ampModule) return GPU_ERROR_INTERNAL;

        if (SLANG_FAILED(ampModule->findEntryPointByName("amplificationMain", ampEntry.writeRef()))) {
            if (SLANG_FAILED(ampModule->findEntryPointByName("main", ampEntry.writeRef()))) {
                return GPU_ERROR_INVALID_ARGS;
            }
        }
    }

    std::vector<slang::IComponentType*> componentTypes;
    componentTypes.push_back(meshModule.get());
    if (hasAmplification) componentTypes.push_back(ampModule.get());
    componentTypes.push_back(fragModule.get());

    rhi::ComPtr<slang::IComponentType> linkedProgram;
    rhi::ComPtr<slang::IBlob> linkDiag;
    if (SLANG_FAILED(slangSession->createCompositeComponentType(
        componentTypes.data(), (uint32_t)componentTypes.size(), linkedProgram.writeRef(), linkDiag.writeRef()))) {
        if (linkDiag) {
            fprintf(stderr, "gpuCreateMeshPipeline: link failed: %.*s\n",
                (int)linkDiag->getBufferSize(), (const char*)linkDiag->getBufferPointer());
        }
        return GPU_ERROR_INTERNAL;
    }

    std::vector<slang::IComponentType*> entryPoints;
    entryPoints.push_back(meshEntry.get());
    if (hasAmplification) entryPoints.push_back(ampEntry.get());
    entryPoints.push_back(fragEntry.get());

    rhi::ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram.get();
    programDesc.slangEntryPoints = entryPoints.data();
    programDesc.slangEntryPointCount = (uint32_t)entryPoints.size();

    rhi::ComPtr<rhi::IShaderProgram> rhiProgram;
    if (SLANG_FAILED(device->rhiDevice->createShaderProgram(programDesc, rhiProgram.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    rhi::RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = rhiProgram;
    pipelineDesc.primitiveTopology = rhi::PrimitiveTopology::TriangleList;
    pipelineDesc.label = desc->label;

    std::vector<rhi::ColorTargetDesc> targets;
    for (uint32_t i = 0; i < desc->colorTargetCount && i < 8; i++) {
        rhi::ColorTargetDesc target = {};
        target.format = gpuFormatToRhi(desc->colorTargets[i].format);
        target.enableBlend = desc->colorTargets[i].blend.blendEnable;
        targets.push_back(target);
    }
    pipelineDesc.targets = targets.data();
    pipelineDesc.targetCount = (uint32_t)targets.size();

    if (desc->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
        pipelineDesc.depthStencil.format = gpuFormatToRhi(desc->depthStencilFormat);
        pipelineDesc.depthStencil.depthTestEnable = true;
        pipelineDesc.depthStencil.depthWriteEnable = true;
    }

    pipelineDesc.multisample.sampleCount = desc->sampleCount > 0 ? desc->sampleCount : 1;

    rhi::ComPtr<rhi::IRenderPipeline> rhiPipeline;
    if (SLANG_FAILED(device->rhiDevice->createRenderPipeline(pipelineDesc, rhiPipeline.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    uint32_t idx = device->pipelinePool.allocate(rhiPipeline.detach());
    if (idx == 0) return GPU_ERROR_OUT_OF_MEMORY;

    outPipeline->index = idx;
    outPipeline->generation = device->pipelinePool.slots[idx].generation;
    return GPU_SUCCESS;
}

void gpuCmdDrawMeshTasks(GpuCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (!cmd || !cmd->rhiEncoder) return;
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    cmd->renderPassEncoder->drawMeshTasks(groupCountX, groupCountY, groupCountZ);
}

void gpuCmdDrawMeshTasksIndirect(GpuCommandBuffer cmd, GpuBufferHandle indirectBuffer, uint64_t offset) {
    if (!cmd || !indirectBuffer.index) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::IBuffer* rhiBuf = cmd->device->bufferPool.resolve(indirectBuffer.index, indirectBuffer.generation);
    if (!rhiBuf) return;
    void* mapped = nullptr;
    if (SLANG_SUCCEEDED(cmd->device->rhiDevice->mapBuffer(rhiBuf, rhi::CpuAccessMode::Read, &mapped))) {
        uint32_t args[3] = {};
        memcpy(args, (uint8_t*)mapped + offset, sizeof(uint32_t) * 3);
        cmd->device->rhiDevice->unmapBuffer(rhiBuf);
        if (args[0] > 0 && args[1] > 0 && args[2] > 0) {
            cmd->renderPassEncoder->drawMeshTasks(args[0], args[1], args[2]);
        }
    }
}

void gpuCmdDrawMeshTasksIndirectCount(GpuCommandBuffer cmd,
                                        GpuBufferHandle indirectBuffer,
                                        uint64_t indirectOffset,
                                        GpuBufferHandle countBuffer,
                                        uint64_t countOffset,
                                        uint32_t maxDrawCount) {
    if (!cmd || !indirectBuffer.index) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::IBuffer* rhiIndirect = cmd->device->bufferPool.resolve(indirectBuffer.index, indirectBuffer.generation);
    rhi::IBuffer* rhiCount = cmd->device->bufferPool.resolve(countBuffer.index, countBuffer.generation);
    if (!rhiIndirect) return;
    uint32_t drawCount = maxDrawCount;
    if (rhiCount) {
        void* countMapped = nullptr;
        if (SLANG_SUCCEEDED(cmd->device->rhiDevice->mapBuffer(rhiCount, rhi::CpuAccessMode::Read, &countMapped))) {
            uint32_t count = 0;
            memcpy(&count, (uint8_t*)countMapped + countOffset, sizeof(uint32_t));
            cmd->device->rhiDevice->unmapBuffer(rhiCount);
            if (count < drawCount) drawCount = count;
        }
    }
    void* mapped = nullptr;
    if (SLANG_SUCCEEDED(cmd->device->rhiDevice->mapBuffer(rhiIndirect, rhi::CpuAccessMode::Read, &mapped))) {
        for (uint32_t i = 0; i < drawCount; i++) {
            uint32_t args[3] = {};
            memcpy(args, (uint8_t*)mapped + indirectOffset + i * sizeof(uint32_t) * 3, sizeof(uint32_t) * 3);
            if (args[0] > 0 && args[1] > 0 && args[2] > 0) {
                cmd->renderPassEncoder->drawMeshTasks(args[0], args[1], args[2]);
            }
        }
        cmd->device->rhiDevice->unmapBuffer(rhiIndirect);
    }
}

GpuResult gpuGetMeshShaderLimits(GpuDevice device, GpuMeshShaderLimits* outLimits) {
    if (!device || !outLimits) return GPU_ERROR_INVALID_ARGS;

    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    if (!caps.supportMeshShader) return GPU_ERROR_NOT_SUPPORTED;

    outLimits->maxMeshOutputVertices = 256;
    outLimits->maxMeshOutputPrimitives = 256;
    outLimits->maxMeshOutputMemory = 32768;
    outLimits->maxMeshWorkGroupSize = 128;
    outLimits->maxTaskWorkGroupSize = 128;
    outLimits->maxTaskPayloadSize = 16384;

    return GPU_OK;
}
