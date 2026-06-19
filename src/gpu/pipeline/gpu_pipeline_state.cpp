#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/core/gpu_pipeline.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include <string.h>
#include <slang-rhi.h>
#include <slang.h>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#ifdef _MSC_VER
#pragma warning(disable : 4996)
#endif

// Storage pools for pipeline states
static GpuHandlePool<rhi::IRenderPipeline> g_renderPipelinePool;
static GpuHandlePool<rhi::IComputePipeline> g_computePipelinePool;

// Internal pipeline type storage in generation bits
#define PIPELINE_TYPE_BITS 2
#define PIPELINE_TYPE_MASK ((1 << PIPELINE_TYPE_BITS) - 1)

// Encode type into generation field
static inline uint32_t encodeTypeInGeneration(GpuPipelineType type, uint32_t baseGen) {
    return (baseGen << PIPELINE_TYPE_BITS) | ((uint32_t)type & PIPELINE_TYPE_MASK);
}

// Decode type from generation field
static inline GpuPipelineType decodeTypeFromGeneration(uint32_t generation) {
    return (GpuPipelineType)(generation & PIPELINE_TYPE_MASK);
}

// Get base generation without type encoding
static inline uint32_t baseGeneration(uint32_t generation) {
    return generation >> PIPELINE_TYPE_BITS;
}

// Helper to convert primitive topology
static inline rhi::PrimitiveTopology convertTopology(GpuPrimitiveTopology topo) {
    switch (topo) {
    case GPU_PRIMITIVE_TOPOLOGY_POINT_LIST:     return rhi::PrimitiveTopology::PointList;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_LIST:      return rhi::PrimitiveTopology::LineList;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP:     return rhi::PrimitiveTopology::LineStrip;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  return rhi::PrimitiveTopology::TriangleList;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return rhi::PrimitiveTopology::TriangleStrip;
    default:                                    return rhi::PrimitiveTopology::TriangleList;
    }
}

// Helper to convert cull mode
static inline rhi::CullMode convertCullMode(GpuCullMode mode) {
    switch (mode) {
    case GPU_CULL_MODE_NONE:  return rhi::CullMode::None;
    case GPU_CULL_MODE_FRONT: return rhi::CullMode::Front;
    case GPU_CULL_MODE_BACK:  return rhi::CullMode::Back;
    default:                  return rhi::CullMode::None;
    }
}

// Helper to convert polygon mode (FillMode in RHI)
static inline rhi::FillMode convertFillMode(GpuPolygonMode mode) {
    switch (mode) {
    case GPU_POLYGON_MODE_FILL:  return rhi::FillMode::Solid;
    case GPU_POLYGON_MODE_LINE:  return rhi::FillMode::Wireframe;
    default:                     return rhi::FillMode::Solid;
    }
}

// Helper to convert compare op
static inline rhi::ComparisonFunc convertCompareOp(GpuCompareOp op) {
    switch (op) {
    case GPU_COMPARE_OP_NEVER:          return rhi::ComparisonFunc::Never;
    case GPU_COMPARE_OP_LESS:           return rhi::ComparisonFunc::Less;
    case GPU_COMPARE_OP_EQUAL:          return rhi::ComparisonFunc::Equal;
    case GPU_COMPARE_OP_LESS_EQUAL:     return rhi::ComparisonFunc::LessEqual;
    case GPU_COMPARE_OP_GREATER:        return rhi::ComparisonFunc::Greater;
    case GPU_COMPARE_OP_NOT_EQUAL:      return rhi::ComparisonFunc::NotEqual;
    case GPU_COMPARE_OP_GREATER_EQUAL:  return rhi::ComparisonFunc::GreaterEqual;
    case GPU_COMPARE_OP_ALWAYS:         return rhi::ComparisonFunc::Always;
    default:                            return rhi::ComparisonFunc::Always;
    }
}

// Helper to convert blend factor
static inline rhi::BlendFactor convertBlendFactor(GpuBlendFactor factor) {
    switch (factor) {
    case GPU_BLEND_FACTOR_ZERO:                     return rhi::BlendFactor::Zero;
    case GPU_BLEND_FACTOR_ONE:                      return rhi::BlendFactor::One;
    case GPU_BLEND_FACTOR_SRC_COLOR:                return rhi::BlendFactor::SrcColor;
    case GPU_BLEND_FACTOR_ONE_MINUS_SRC_COLOR:      return rhi::BlendFactor::InvSrcColor;
    case GPU_BLEND_FACTOR_DST_COLOR:                return rhi::BlendFactor::DestColor;
    case GPU_BLEND_FACTOR_ONE_MINUS_DST_COLOR:      return rhi::BlendFactor::InvDestColor;
    case GPU_BLEND_FACTOR_SRC_ALPHA:                return rhi::BlendFactor::SrcAlpha;
    case GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:      return rhi::BlendFactor::InvSrcAlpha;
    case GPU_BLEND_FACTOR_DST_ALPHA:                return rhi::BlendFactor::DestAlpha;
    case GPU_BLEND_FACTOR_ONE_MINUS_DST_ALPHA:      return rhi::BlendFactor::InvDestAlpha;
    case GPU_BLEND_FACTOR_CONSTANT_COLOR:           return rhi::BlendFactor::BlendColor;
    case GPU_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR: return rhi::BlendFactor::InvBlendColor;
    case GPU_BLEND_FACTOR_CONSTANT_ALPHA:           return rhi::BlendFactor::BlendColor;
    case GPU_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA: return rhi::BlendFactor::InvBlendColor;
    default:                                        return rhi::BlendFactor::One;
    }
}

// Helper to convert blend op
static inline rhi::BlendOp convertBlendOp(GpuBlendOp op) {
    switch (op) {
    case GPU_BLEND_OP_ADD:              return rhi::BlendOp::Add;
    case GPU_BLEND_OP_SUBTRACT:         return rhi::BlendOp::Subtract;
    case GPU_BLEND_OP_REVERSE_SUBTRACT: return rhi::BlendOp::ReverseSubtract;
    case GPU_BLEND_OP_MIN:              return rhi::BlendOp::Min;
    case GPU_BLEND_OP_MAX:              return rhi::BlendOp::Max;
    default:                            return rhi::BlendOp::Add;
    }
}

// Convert vertex format to RHI
static inline rhi::Format convertVertexFormat(GpuFormat fmt) {
    return gpuFormatToRhi(fmt);
}

// ============================================================================
// Graphics Pipeline Creation
// ============================================================================

extern "C" GpuResult gpuCreateGraphicsPipeline(GpuDevice device, const GpuGraphicsPipelineDesc* desc, GpuPipelineHandle* outPipeline) {
    if (!device || !desc || !outPipeline) {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    rhi::ComPtr<rhi::IShaderProgram> rhiProgram;

    bool hasVertexShader = desc->vertexShader.data && desc->vertexShader.size > 0;
    bool hasFragmentShader = desc->fragmentShader.data && desc->fragmentShader.size > 0;

    if (hasVertexShader || hasFragmentShader) {
        rhi::ComPtr<slang::ISession> slangSession;
        if (SLANG_FAILED(device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
            return GPU_ERROR_INTERNAL;
        }

        std::string vsSrc((const char*)desc->vertexShader.data, (size_t)desc->vertexShader.size);
        std::string fsSrc((const char*)desc->fragmentShader.data, (size_t)desc->fragmentShader.size);

        char tempDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tempDir);
        std::string vsPath = std::string(tempDir) + "gpu_vs.slang";
        std::string fsPath = std::string(tempDir) + "gpu_fs.slang";

        std::vector<rhi::ComPtr<slang::IModule>> modules;
        std::vector<rhi::ComPtr<slang::IEntryPoint>> entryPoints;
        std::vector<slang::IComponentType*> componentTypes;

        if (hasVertexShader) {
            FILE* f = fopen(vsPath.c_str(), "w");
            if (f) { fputs(vsSrc.c_str(), f); fclose(f); }

            rhi::ComPtr<slang::IModule> vsModule;
            slang::IBlob* vsDiag = nullptr;
            vsModule = slangSession->loadModule(vsPath.c_str(), &vsDiag);
            if (vsDiag) vsDiag->release();
            if (vsModule) {
                modules.push_back(vsModule);
                componentTypes.push_back(vsModule.get());
                rhi::ComPtr<slang::IEntryPoint> vsEntry;
                if (SLANG_SUCCEEDED(vsModule->findEntryPointByName("vertexMain", vsEntry.writeRef())) ||
                    SLANG_SUCCEEDED(vsModule->findEntryPointByName("main", vsEntry.writeRef()))) {
                    entryPoints.push_back(vsEntry);
                }
            }
        }

        if (hasFragmentShader) {
            FILE* f = fopen(fsPath.c_str(), "w");
            if (f) { fputs(fsSrc.c_str(), f); fclose(f); }

            rhi::ComPtr<slang::IModule> fsModule;
            slang::IBlob* fsDiag = nullptr;
            fsModule = slangSession->loadModule(fsPath.c_str(), &fsDiag);
            if (fsDiag) fsDiag->release();
            if (fsModule) {
                modules.push_back(fsModule);
                componentTypes.push_back(fsModule.get());
                rhi::ComPtr<slang::IEntryPoint> fsEntry;
                if (SLANG_SUCCEEDED(fsModule->findEntryPointByName("fragmentMain", fsEntry.writeRef())) ||
                    SLANG_SUCCEEDED(fsModule->findEntryPointByName("main", fsEntry.writeRef()))) {
                    entryPoints.push_back(fsEntry);
                }
            }
        }

        if (!componentTypes.empty()) {
            std::vector<slang::IComponentType*> rawComponentsPlusEntries;
            for (auto ct : componentTypes) rawComponentsPlusEntries.push_back(ct);
            for (auto& ep : entryPoints) rawComponentsPlusEntries.push_back(ep.get());

            rhi::ComPtr<slang::IComponentType> linkedProgram;
            rhi::ComPtr<slang::IBlob> linkDiag;
            slangSession->createCompositeComponentType(
                rawComponentsPlusEntries.data(), (uint32_t)rawComponentsPlusEntries.size(),
                linkedProgram.writeRef(), linkDiag.writeRef());

            if (linkedProgram) {
                rhi::ShaderProgramDesc programDesc = {};
                programDesc.slangGlobalScope = linkedProgram.get();

                std::vector<slang::IComponentType*> rawEntries;
                for (auto& ep : entryPoints) rawEntries.push_back(ep.get());
                programDesc.slangEntryPoints = rawEntries.data();
                programDesc.slangEntryPointCount = (uint32_t)rawEntries.size();

                device->rhiDevice->createShaderProgram(programDesc, rhiProgram.writeRef());
            }
        }
    }

    rhi::RenderPipelineDesc rhiDesc = {};
    rhiDesc.program = rhiProgram;
    rhiDesc.primitiveTopology = convertTopology(desc->primitiveTopology);
    rhiDesc.label = desc->label;

    std::vector<rhi::ColorTargetDesc> targets;
    for (uint32_t i = 0; i < desc->colorTargetCount; i++) {
        rhi::ColorTargetDesc target = {};
        target.format = gpuFormatToRhi(desc->colorTargets[i].format);
        target.enableBlend = desc->colorTargets[i].blend.blendEnable;
        target.color.srcFactor = convertBlendFactor(desc->colorTargets[i].blend.srcColorBlendFactor);
        target.color.dstFactor = convertBlendFactor(desc->colorTargets[i].blend.dstColorBlendFactor);
        target.color.op = convertBlendOp(desc->colorTargets[i].blend.colorBlendOp);
        target.alpha.srcFactor = convertBlendFactor(desc->colorTargets[i].blend.srcAlphaBlendFactor);
        target.alpha.dstFactor = convertBlendFactor(desc->colorTargets[i].blend.dstAlphaBlendFactor);
        target.alpha.op = convertBlendOp(desc->colorTargets[i].blend.alphaBlendOp);
        target.writeMask = (rhi::RenderTargetWriteMask)(desc->colorTargets[i].blend.colorWriteMask & 0xF);
        targets.push_back(target);
    }
    rhiDesc.targets = targets.data();
    rhiDesc.targetCount = desc->colorTargetCount;

    rhiDesc.rasterizer.cullMode = convertCullMode(desc->cullMode);
    rhiDesc.rasterizer.frontFace = (desc->frontFace == GPU_FRONT_FACE_CLOCKWISE)
                                    ? rhi::FrontFaceMode::Clockwise
                                    : rhi::FrontFaceMode::CounterClockwise;
    rhiDesc.rasterizer.fillMode = convertFillMode(desc->polygonMode);

    rhiDesc.depthStencil.depthTestEnable = desc->depthTestEnable;
    rhiDesc.depthStencil.depthWriteEnable = desc->depthWriteEnable;
    rhiDesc.depthStencil.depthFunc = convertCompareOp(desc->depthCompareOp);
    if (desc->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
        rhiDesc.depthStencil.format = gpuFormatToRhi(desc->depthStencilFormat);
    }

    rhiDesc.multisample.sampleCount = desc->sampleCount > 0 ? desc->sampleCount : 1;

    rhi::ComPtr<rhi::IRenderPipeline> rhiPipeline;
    rhi::Result r = device->rhiDevice->createRenderPipeline(rhiDesc, rhiPipeline.writeRef());

    if (SLANG_FAILED(r)) {
        return GPU_ERROR_UNKNOWN;
    }

    uint32_t index = g_renderPipelinePool.allocate(rhiPipeline.detach());
    if (index == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outPipeline->index = index;
    outPipeline->generation = encodeTypeInGeneration(GPU_PIPELINE_TYPE_GRAPHICS, 1);

    return GPU_OK;
}

// ============================================================================
// Compute Pipeline Creation
// ============================================================================

extern "C" GpuResult gpuCreateComputePipeline2(GpuDevice device, const GpuComputePipelineDesc2* desc, GpuPipelineHandle* outPipeline) {
    if (!device || !desc || !outPipeline) {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    rhi::ComPtr<slang::ISession> slangSession;
    if (SLANG_FAILED(device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    rhi::ComPtr<slang::IModule> csModule;
    if (desc->computeShader.data && desc->computeShader.size > 0) {
        std::string csSrc((const char*)desc->computeShader.data, (size_t)desc->computeShader.size);

        char tempDir[MAX_PATH];
        GetTempPathA(MAX_PATH, tempDir);
        std::string csPath = std::string(tempDir) + "gpu_cs.slang";

        FILE* f = fopen(csPath.c_str(), "w");
        if (f) { fputs(csSrc.c_str(), f); fclose(f); }

        slang::IBlob* csDiag = nullptr;
        csModule = slangSession->loadModule(csPath.c_str(), &csDiag);
        if (csDiag) csDiag->release();
        if (!csModule) {
            return GPU_ERROR_INTERNAL;
        }
    } else {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    rhi::ComPtr<slang::IEntryPoint> csEntry;
    if (SLANG_FAILED(csModule->findEntryPointByName("main", csEntry.writeRef()))) {
        return GPU_ERROR_INVALID_ARGS;
    }

    slang::IComponentType* components[] = { csModule.get(), csEntry.get() };
    rhi::ComPtr<slang::IComponentType> linked;
    rhi::ComPtr<slang::IBlob> linkDiag;
    slangSession->createCompositeComponentType(components, 2, linked.writeRef(), linkDiag.writeRef());

    if (!linked) return GPU_ERROR_INTERNAL;

    rhi::ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = linked.get();
    slang::IComponentType* entries[] = { csEntry.get() };
    programDesc.slangEntryPoints = entries;
    programDesc.slangEntryPointCount = 1;

    rhi::ComPtr<rhi::IShaderProgram> rhiProgram;
    if (SLANG_FAILED(device->rhiDevice->createShaderProgram(programDesc, rhiProgram.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    rhi::ComputePipelineDesc rhiDesc = {};
    rhiDesc.program = rhiProgram;
    rhiDesc.label = desc->label;

    rhi::ComPtr<rhi::IComputePipeline> rhiPipeline;
    rhi::Result r = device->rhiDevice->createComputePipeline(rhiDesc, rhiPipeline.writeRef());

    if (SLANG_FAILED(r)) {
        return GPU_ERROR_UNKNOWN;
    }

    uint32_t index = g_computePipelinePool.allocate(rhiPipeline.detach());
    if (index == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outPipeline->index = index;
    outPipeline->generation = encodeTypeInGeneration(GPU_PIPELINE_TYPE_COMPUTE, 1);

    return GPU_OK;
}

// ============================================================================
// Compute Pipeline Creation from Program
// ============================================================================

extern "C" GpuResult gpuCreateComputePipelineFromProgram(GpuDevice device, GpuShaderProgram program, const char* label, GpuPipelineHandle* outPipeline) {
    if (!device || !program || !outPipeline) {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    // Build RHI compute pipeline state using the shader program
    rhi::ComputePipelineDesc rhiDesc = {};
    rhiDesc.structType = rhi::StructType::ComputePipelineDesc;
    rhiDesc.program = program->rhiProgram;
    rhiDesc.label = label;

    rhi::ComPtr<rhi::IComputePipeline> rhiPipeline;
    rhi::Result r = device->rhiDevice->createComputePipeline(rhiDesc, rhiPipeline.writeRef());
    
    if (SLANG_FAILED(r)) {
        return GPU_ERROR_UNKNOWN;
    }

    // Store in pool
    uint32_t index = g_computePipelinePool.allocate(rhiPipeline.detach());
    if (index == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    
    uint32_t baseGen = 1;
    outPipeline->index = index;
    outPipeline->generation = encodeTypeInGeneration(GPU_PIPELINE_TYPE_COMPUTE, baseGen);

    return GPU_OK;
}

// ============================================================================
// Pipeline Destruction
// ============================================================================

extern "C" GpuResult gpuDestroyPipeline(GpuDevice device, GpuPipelineHandle pipeline) {
    if (!device) {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    if (!gpuHandleIsValid(pipeline)) {
        return GPU_OK;
    }

    GpuPipelineType type = decodeTypeFromGeneration(pipeline.generation);
    uint32_t gen = baseGeneration(pipeline.generation);

    switch (type) {
    case GPU_PIPELINE_TYPE_GRAPHICS:
        g_renderPipelinePool.release(pipeline.index, gen);
        break;
    case GPU_PIPELINE_TYPE_COMPUTE:
        g_computePipelinePool.release(pipeline.index, gen);
        break;
    case GPU_PIPELINE_TYPE_RAYTRACING:
        break;
    }

    return GPU_OK;
}

// ============================================================================
// Pipeline Type Query
// ============================================================================

extern "C" GpuPipelineType gpuGetPipelineType(GpuDevice device, GpuPipelineHandle pipeline) {
    (void)device;
    if (!gpuHandleIsValid(pipeline)) {
        return GPU_PIPELINE_TYPE_GRAPHICS;
    }
    return decodeTypeFromGeneration(pipeline.generation);
}

// ============================================================================
// Internal Resolve Functions
// ============================================================================

rhi::IRenderPipeline* gpuResolveRenderPipeline(GpuDevice device, GpuPipelineHandle pipeline) {
    (void)device;
    GpuPipelineType type = decodeTypeFromGeneration(pipeline.generation);
    uint32_t gen = baseGeneration(pipeline.generation);
    
    if (type != GPU_PIPELINE_TYPE_GRAPHICS) {
        return nullptr;
    }
    return g_renderPipelinePool.resolve(pipeline.index, gen);
}

rhi::IComputePipeline* gpuResolveComputePipeline(GpuDevice device, GpuPipelineHandle pipeline) {
    (void)device;
    GpuPipelineType type = decodeTypeFromGeneration(pipeline.generation);
    uint32_t gen = baseGeneration(pipeline.generation);
    
    if (type != GPU_PIPELINE_TYPE_COMPUTE) {
        return nullptr;
    }
    return g_computePipelinePool.resolve(pipeline.index, gen);
}
