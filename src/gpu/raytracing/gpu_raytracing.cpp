#include "gpu/raytracing/gpu_raytracing.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/debug/gpu_validation.h"
#include <slang-rhi.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

static GpuHandlePool<rhi::IShaderTable> g_shaderTablePool;
static GpuHandlePool<rhi::IRayTracingPipeline> g_rtPipelinePool;
static std::map<uint32_t, uint32_t> g_pipelineToShaderTable;

static inline uint32_t encodeTypeInGeneration(GpuPipelineType type, uint32_t baseGen) {
    return (baseGen << 2) | ((uint32_t)type & 3);
}

static inline uint32_t baseGenFromHandle(GpuPipelineHandle h) {
    return h.generation >> 2;
}

GpuResult gpuCreateBottomLevelAS(GpuDevice device, const GpuBottomLevelASDesc* desc, GpuAccelerationStructureHandle* outAS)
{
    if (!device || !desc || !outAS) return GPU_ERROR_INVALID_ARGS;
    if (!desc->geometryCount || !desc->geometries) return GPU_ERROR_INVALID_ARGS;

    if (!device->rhiDevice->hasFeature(rhi::Feature::AccelerationStructure)) {
        GPU_FEATURE_GATE(device, GPU_FEATURE_RAY_TRACING, "BLAS");
    }

    std::vector<rhi::AccelerationStructureBuildInput> buildInputs;

    for (uint32_t i = 0; i < desc->geometryCount; i++) {
        const GpuAccelerationStructureGeometry& geom = desc->geometries[i];

        rhi::AccelerationStructureBuildInput input = {};
        input.type = rhi::AccelerationStructureBuildInputType::Triangles;

        rhi::IBuffer* vertexBuffer = nullptr;
        if (gpuHandleIsValid(geom.vertexBuffer)) {
            vertexBuffer = device->bufferPool.resolve(geom.vertexBuffer.index, geom.vertexBuffer.generation);
        }
        input.triangles.vertexBuffers[0] = rhi::BufferOffsetPair(vertexBuffer, 0);
        input.triangles.vertexBufferCount = vertexBuffer ? 1 : 0;
        input.triangles.vertexFormat = gpuFormatToRhi(geom.vertexFormat);
        input.triangles.vertexCount = geom.triangleCount * 3;
        input.triangles.vertexStride = geom.vertexStride;

        rhi::IBuffer* indexBuffer = nullptr;
        if (gpuHandleIsValid(geom.indexBuffer)) {
            indexBuffer = device->bufferPool.resolve(geom.indexBuffer.index, geom.indexBuffer.generation);
        }
        input.triangles.indexBuffer = rhi::BufferOffsetPair(indexBuffer, 0);
        input.triangles.indexFormat = rhi::IndexFormat::Uint32;
        input.triangles.indexCount = indexBuffer ? geom.triangleCount * 3 : 0;

        buildInputs.push_back(input);
    }

    rhi::AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = buildInputs.data();
    buildDesc.inputCount = (uint32_t)buildInputs.size();
    buildDesc.mode = rhi::AccelerationStructureBuildMode::Build;
    buildDesc.flags = desc->allowUpdate ? rhi::AccelerationStructureBuildFlags::AllowUpdate : rhi::AccelerationStructureBuildFlags::None;

    rhi::AccelerationStructureSizes sizes;
    rhi::Result r = device->rhiDevice->getAccelerationStructureSizes(buildDesc, &sizes);
    if (SLANG_FAILED(r)) return GPU_ERROR_INTERNAL;

    rhi::AccelerationStructureDesc asDesc = {};
    asDesc.kind = rhi::AccelerationStructureKind::BottomLevel;
    asDesc.size = sizes.accelerationStructureSize;
    asDesc.flags = buildDesc.flags;
    asDesc.label = "BLAS";

    rhi::ComPtr<rhi::IAccelerationStructure> rhiAS;
    r = device->rhiDevice->createAccelerationStructure(asDesc, rhiAS.writeRef());
    if (SLANG_FAILED(r)) return GPU_ERROR_INTERNAL;

    uint32_t idx = device->accelStructPool.allocate(rhiAS.detach());
    if (idx == 0) return GPU_ERROR_OUT_OF_MEMORY;

    outAS->index = idx;
    outAS->generation = device->accelStructPool.slots[idx].generation;
    return GPU_SUCCESS;
}

GpuResult gpuCreateTopLevelAS(GpuDevice device, const GpuTopLevelASDesc* desc, GpuAccelerationStructureHandle* outAS)
{
    if (!device || !desc || !outAS) return GPU_ERROR_INVALID_ARGS;

    if (!device->rhiDevice->hasFeature(rhi::Feature::AccelerationStructure)) {
        GPU_FEATURE_GATE(device, GPU_FEATURE_RAY_TRACING, "TLAS");
    }

    rhi::IBuffer* instanceBuffer = nullptr;
    if (gpuHandleIsValid(desc->instanceBuffer)) {
        instanceBuffer = device->bufferPool.resolve(desc->instanceBuffer.index, desc->instanceBuffer.generation);
    }

    rhi::AccelerationStructureBuildInput input = {};
    input.type = rhi::AccelerationStructureBuildInputType::Instances;
    input.instances.instanceBuffer = rhi::BufferOffsetPair(instanceBuffer, 0);
    input.instances.instanceCount = desc->instanceCount;
    input.instances.instanceStride = sizeof(rhi::AccelerationStructureInstanceDescGeneric);

    rhi::AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = &input;
    buildDesc.inputCount = 1;
    buildDesc.mode = rhi::AccelerationStructureBuildMode::Build;

    rhi::AccelerationStructureSizes sizes;
    rhi::Result r = device->rhiDevice->getAccelerationStructureSizes(buildDesc, &sizes);
    if (SLANG_FAILED(r)) return GPU_ERROR_INTERNAL;

    rhi::AccelerationStructureDesc asDesc = {};
    asDesc.kind = rhi::AccelerationStructureKind::TopLevel;
    asDesc.size = sizes.accelerationStructureSize;
    asDesc.label = "TLAS";

    rhi::ComPtr<rhi::IAccelerationStructure> rhiAS;
    r = device->rhiDevice->createAccelerationStructure(asDesc, rhiAS.writeRef());
    if (SLANG_FAILED(r)) return GPU_ERROR_INTERNAL;

    uint32_t idx = device->accelStructPool.allocate(rhiAS.detach());
    if (idx == 0) return GPU_ERROR_OUT_OF_MEMORY;

    outAS->index = idx;
    outAS->generation = device->accelStructPool.slots[idx].generation;
    return GPU_SUCCESS;
}

GpuResult gpuBuildAccelerationStructure(GpuCommandBuffer cmd, GpuAccelerationStructureHandle as, bool update)
{
    if (!cmd || !as.index) return GPU_ERROR_INVALID_ARGS;

    rhi::IAccelerationStructure* rhiAS = cmd->device->accelStructPool.resolve(as.index, as.generation);
    if (!rhiAS) return GPU_ERROR_INVALID_ARGS;

    if (!cmd->rhiEncoder) return GPU_ERROR_INTERNAL;

    rhi::AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.mode = update ? rhi::AccelerationStructureBuildMode::Update : rhi::AccelerationStructureBuildMode::Build;

    GpuBufferHandle scratchBuf = {};
    GpuBufferDesc scratchDesc = {};
    scratchDesc.size = 65536;
    scratchDesc.usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS;
    GpuResult res = gpuCreateBuffer(cmd->device, &scratchDesc, &scratchBuf);
    if (res != GPU_SUCCESS) return res;

    rhi::IBuffer* scratchRhi = cmd->device->bufferPool.resolve(scratchBuf.index, scratchBuf.generation);

    cmd->rhiEncoder->buildAccelerationStructure(
        buildDesc,
        rhiAS,
        update ? rhiAS : nullptr,
        rhi::BufferOffsetPair(scratchRhi, 0),
        0,
        nullptr
    );

    gpuDestroyBuffer(cmd->device, scratchBuf);
    return GPU_SUCCESS;
}

GpuResult gpuDestroyAccelerationStructure(GpuDevice device, GpuAccelerationStructureHandle as)
{
    if (!device || !as.index) return GPU_SUCCESS;

    rhi::IAccelerationStructure* rhiAS = device->accelStructPool.resolve(as.index, as.generation);
    if (!rhiAS) return GPU_ERROR_INVALID_ARGS;

    rhiAS->release();
    device->accelStructPool.release(as.index, as.generation);
    return GPU_SUCCESS;
}

static GpuResult loadModuleFromBinary(
    slang::ISession* slangSession,
    const GpuShaderBinary& binary,
    const char* tempSuffix,
    rhi::ComPtr<slang::IModule>& outModule)
{
    if (!binary.data || binary.size == 0) return GPU_ERROR_INVALID_ARGS;

    std::string src((const char*)binary.data, (size_t)binary.size);

    char tempDir[MAX_PATH];
    GetTempPathA(MAX_PATH, tempDir);
    std::string path = std::string(tempDir) + "gpu_rt_" + tempSuffix + ".slang";

    FILE* f = fopen(path.c_str(), "w");
    if (!f) return GPU_ERROR_INTERNAL;
    fputs(src.c_str(), f);
    fclose(f);

    slang::IBlob* diagBlob = nullptr;
    outModule = slangSession->loadModule(path.c_str(), &diagBlob);
    if (diagBlob) diagBlob->release();
    if (!outModule) return GPU_ERROR_INTERNAL;

    return GPU_OK;
}
GpuResult gpuCreateRayTracingPipeline(GpuDevice device, const GpuRayTracingPipelineDesc* desc, GpuPipelineHandle* outPipeline)
{
    if (!device || !desc || !outPipeline) return GPU_ERROR_INVALID_ARGS;

    if (!device->rhiDevice->hasFeature(rhi::Feature::RayTracing)) {
        GPU_FEATURE_GATE(device, GPU_FEATURE_RAY_TRACING, desc->label);
    }

    rhi::ComPtr<slang::ISession> slangSession;
    if (SLANG_FAILED(device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    std::vector<rhi::ComPtr<slang::IModule>> modules;
    std::vector<rhi::ComPtr<slang::IEntryPoint>> entryPoints;
    std::vector<slang::IComponentType*> componentTypes;

    bool hasRayGen = desc->rayGenShader.data && desc->rayGenShader.size > 0;
    bool hasMiss = desc->missShader.data && desc->missShader.size > 0;
    bool hasClosestHit = desc->closestHitShader.data && desc->closestHitShader.size > 0;
    bool hasAnyHit = desc->anyHitShader.data && desc->anyHitShader.size > 0;
    bool hasIntersection = desc->intersectionShader.data && desc->intersectionShader.size > 0;

    if (hasRayGen) {
        rhi::ComPtr<slang::IModule> mod;
        GpuResult r = loadModuleFromBinary(slangSession, desc->rayGenShader, "rgen", mod);
        if (r != GPU_OK) return r;
        modules.push_back(mod);
        componentTypes.push_back(mod.get());
        rhi::ComPtr<slang::IEntryPoint> ep;
        if (SLANG_SUCCEEDED(mod->findEntryPointByName("rayGenMain", ep.writeRef())) ||
            SLANG_SUCCEEDED(mod->findEntryPointByName("main", ep.writeRef()))) {
            entryPoints.push_back(ep);
        }
    }

    if (hasMiss) {
        rhi::ComPtr<slang::IModule> mod;
        GpuResult r = loadModuleFromBinary(slangSession, desc->missShader, "rmiss", mod);
        if (r != GPU_OK) return r;
        modules.push_back(mod);
        componentTypes.push_back(mod.get());
        rhi::ComPtr<slang::IEntryPoint> ep;
        if (SLANG_SUCCEEDED(mod->findEntryPointByName("missMain", ep.writeRef())) ||
            SLANG_SUCCEEDED(mod->findEntryPointByName("main", ep.writeRef()))) {
            entryPoints.push_back(ep);
        }
    }

    if (hasClosestHit) {
        rhi::ComPtr<slang::IModule> mod;
        GpuResult r = loadModuleFromBinary(slangSession, desc->closestHitShader, "rchit", mod);
        if (r != GPU_OK) return r;
        modules.push_back(mod);
        componentTypes.push_back(mod.get());
        rhi::ComPtr<slang::IEntryPoint> ep;
        if (SLANG_SUCCEEDED(mod->findEntryPointByName("closestHitMain", ep.writeRef())) ||
            SLANG_SUCCEEDED(mod->findEntryPointByName("main", ep.writeRef()))) {
            entryPoints.push_back(ep);
        }
    }

    if (hasAnyHit) {
        rhi::ComPtr<slang::IModule> mod;
        GpuResult r = loadModuleFromBinary(slangSession, desc->anyHitShader, "rahit", mod);
        if (r != GPU_OK) return r;
        modules.push_back(mod);
        componentTypes.push_back(mod.get());
        rhi::ComPtr<slang::IEntryPoint> ep;
        if (SLANG_SUCCEEDED(mod->findEntryPointByName("anyHitMain", ep.writeRef())) ||
            SLANG_SUCCEEDED(mod->findEntryPointByName("main", ep.writeRef()))) {
            entryPoints.push_back(ep);
        }
    }

    if (hasIntersection) {
        rhi::ComPtr<slang::IModule> mod;
        GpuResult r = loadModuleFromBinary(slangSession, desc->intersectionShader, "rint", mod);
        if (r != GPU_OK) return r;
        modules.push_back(mod);
        componentTypes.push_back(mod.get());
        rhi::ComPtr<slang::IEntryPoint> ep;
        if (SLANG_SUCCEEDED(mod->findEntryPointByName("intersectionMain", ep.writeRef())) ||
            SLANG_SUCCEEDED(mod->findEntryPointByName("main", ep.writeRef()))) {
            entryPoints.push_back(ep);
        }
    }

    std::vector<slang::IComponentType*> rawComponents;
    for (auto ct : componentTypes) rawComponents.push_back(ct);
    for (auto& ep : entryPoints) rawComponents.push_back(ep.get());

    rhi::ComPtr<slang::IComponentType> linkedProgram;
    rhi::ComPtr<slang::IBlob> linkDiag;
    slangSession->createCompositeComponentType(
        rawComponents.data(), (uint32_t)rawComponents.size(),
        linkedProgram.writeRef(), linkDiag.writeRef());

    if (!linkedProgram) return GPU_ERROR_INTERNAL;

    rhi::ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linkedProgram.get();

    std::vector<slang::IComponentType*> rawEntries;
    for (auto& ep : entryPoints) rawEntries.push_back(ep.get());
    programDesc.slangEntryPoints = rawEntries.data();
    programDesc.slangEntryPointCount = (uint32_t)rawEntries.size();

    rhi::ComPtr<rhi::IShaderProgram> rhiProgram;
    if (SLANG_FAILED(device->rhiDevice->createShaderProgram(programDesc, rhiProgram.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    rhi::HitGroupDesc hitGroup = {};
    std::string hitGroupName = "hitgroup0";
    hitGroup.hitGroupName = hitGroupName.c_str();
    if (hasClosestHit) hitGroup.closestHitEntryPoint = "closestHitMain";
    if (hasAnyHit) hitGroup.anyHitEntryPoint = "anyHitMain";
    if (hasIntersection) hitGroup.intersectionEntryPoint = "intersectionMain";

    rhi::RayTracingPipelineDesc rtDesc = {};
    rtDesc.program = rhiProgram;
    rtDesc.hitGroupCount = hasClosestHit ? 1 : 0;
    rtDesc.hitGroups = &hitGroup;
    rtDesc.maxRecursion = desc->maxRecursion > 0 ? desc->maxRecursion : 2;
    rtDesc.maxRayPayloadSize = desc->maxRayPayloadSize > 0 ? desc->maxRayPayloadSize : 128;
    rtDesc.label = desc->label;

    rhi::ComPtr<rhi::IRayTracingPipeline> rtPipeline;
    if (SLANG_FAILED(device->rhiDevice->createRayTracingPipeline(rtDesc, rtPipeline.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    uint32_t pipeIdx = g_rtPipelinePool.allocate(rtPipeline.detach());
    if (pipeIdx == 0) return GPU_ERROR_OUT_OF_MEMORY;

    const char* rgNames[] = { "rayGenMain" };
    const char* missNames[] = { "missMain" };
    const char* hgNames[] = { "hitgroup0" };

    rhi::ShaderTableDesc stDesc = {};
    stDesc.program = rhiProgram;
    stDesc.rayGenShaderCount = hasRayGen ? 1 : 0;
    stDesc.rayGenShaderEntryPointNames = rgNames;
    stDesc.missShaderCount = hasMiss ? 1 : 0;
    stDesc.missShaderEntryPointNames = missNames;
    stDesc.hitGroupCount = rtDesc.hitGroupCount;
    stDesc.hitGroupNames = hgNames;

    rhi::ComPtr<rhi::IShaderTable> shaderTable;
    if (SLANG_FAILED(device->rhiDevice->createShaderTable(stDesc, shaderTable.writeRef()))) {
        g_rtPipelinePool.release(pipeIdx, 1);
        return GPU_ERROR_INTERNAL;
    }

    uint32_t stIdx = g_shaderTablePool.allocate(shaderTable.detach());
    if (stIdx == 0) {
        g_rtPipelinePool.release(pipeIdx, 1);
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    g_pipelineToShaderTable[pipeIdx] = stIdx;

    outPipeline->index = pipeIdx;
    outPipeline->generation = encodeTypeInGeneration(GPU_PIPELINE_TYPE_RAYTRACING, 1);

    return GPU_OK;
}

void gpuCmdTraceRays(GpuCommandBuffer cmd, GpuPipelineHandle rtPipeline, uint32_t width, uint32_t height, uint32_t depth)
{
    if (!cmd || !cmd->rhiEncoder || !rtPipeline.index) return;

    if (!cmd->inRayTracingPass) {
        cmd->rtPassEncoder = cmd->rhiEncoder->beginRayTracingPass();
        if (cmd->rtPassEncoder) cmd->inRayTracingPass = true;
    }

    if (!cmd->inRayTracingPass || !cmd->rtPassEncoder) return;

    uint32_t gen = baseGenFromHandle(rtPipeline);
    rhi::IRayTracingPipeline* rtPipe = g_rtPipelinePool.resolve(rtPipeline.index, gen);
    if (!rtPipe) return;

    auto stIt = g_pipelineToShaderTable.find(rtPipeline.index);
    if (stIt == g_pipelineToShaderTable.end()) return;
    rhi::IShaderTable* st = g_shaderTablePool.resolve(stIt->second, g_shaderTablePool.slots[stIt->second].generation);
    if (!st) return;

    cmd->rtPassEncoder->bindPipeline(rtPipe, st);
    cmd->rtPassEncoder->dispatchRays(0, width, height, depth);
}
