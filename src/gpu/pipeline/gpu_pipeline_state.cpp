#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/core/gpu_pipeline.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include <string.h>
#include <slang-rhi.h>
#include <slang.h>
#include <string>
#include <vector>

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
    device->lastError.clear();

    rhi::ComPtr<rhi::IShaderProgram> rhiProgram;

    bool hasVertexShader = desc->vertexShader.data && desc->vertexShader.size > 0;
    bool hasFragmentShader = desc->fragmentShader.data && desc->fragmentShader.size > 0;
    bool hasPrecompiledVertex = hasVertexShader && desc->vertexShader.moduleData && desc->vertexShader.moduleSize > 0;
    bool hasPrecompiledFragment = hasFragmentShader && desc->fragmentShader.moduleData && desc->fragmentShader.moduleSize > 0;

    if (hasVertexShader || hasFragmentShader) {
        rhi::ComPtr<slang::ISession> slangSession;
        if (SLANG_FAILED(device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
            device->lastError = "graphics pipeline could not get Slang session";
            return GPU_ERROR_INTERNAL;
        }

        if ((hasPrecompiledVertex || hasPrecompiledFragment) &&
            (hasVertexShader != hasPrecompiledVertex || hasFragmentShader != hasPrecompiledFragment)) {
            device->lastError = "graphics pipeline mixes precompiled and source stages";
            return GPU_ERROR_INVALID_PARAMETER;
        }

        if (hasPrecompiledVertex || hasPrecompiledFragment) {
            std::vector<rhi::ComPtr<slang::IModule>> modules;
            std::vector<const GpuShaderBinary*> moduleBinaries;
            std::vector<std::string> moduleNames;
            std::vector<rhi::ComPtr<slang::IEntryPoint>> entryPoints;
            std::vector<slang::IComponentType*> moduleComponents;
            std::vector<rhi::ShaderEntryPointCode> precompiledCode;

            auto loadPrecompiled = [&](const GpuShaderBinary& shader, const char* moduleName,
                                       const char* defaultEntry) -> bool {
                const char* serializedName = shader.moduleName ? shader.moduleName : moduleName;
                rhi::ComPtr<slang::IModule> module;
                for (size_t i = 0; i < moduleNames.size(); ++i) {
                    if (moduleNames[i] != serializedName) continue;
                    const GpuShaderBinary& existing = *moduleBinaries[i];
                    if (existing.moduleSize != shader.moduleSize ||
                        memcmp(existing.moduleData, shader.moduleData, (size_t)shader.moduleSize) != 0)
                        return false;
                    module = modules[i];
                    break;
                }
                if (!module) {
                    rhi::ComPtr<ISlangBlob> moduleBlob(slang_createBlob(shader.moduleData, (size_t)shader.moduleSize));
                    if (!moduleBlob) return false;
                    rhi::ComPtr<ISlangBlob> diagnostics;
                    module = slangSession->loadModuleFromIRBlob(
                        serializedName, serializedName, moduleBlob, diagnostics.writeRef());
                    if (!module) {
                        if (diagnostics && diagnostics->getBufferPointer())
                            device->lastError.assign((const char*)diagnostics->getBufferPointer(), diagnostics->getBufferSize());
                        else device->lastError = "Slang rejected serialized module IR";
                        return false;
                    }
                    moduleComponents.push_back(module.get());
                    modules.push_back(module);
                    moduleBinaries.push_back(&shader);
                    moduleNames.emplace_back(serializedName);
                }
                rhi::ComPtr<slang::IEntryPoint> entryPoint;
                const char* entryName = shader.entryPoint ? shader.entryPoint : defaultEntry;
                if (SLANG_FAILED(module->findEntryPointByName(entryName, entryPoint.writeRef()))) {
                    device->lastError = std::string("serialized module has no entry point: ") + entryName;
                    return false;
                }
                entryPoints.push_back(entryPoint);
                precompiledCode.push_back({shader.data, (size_t)shader.size});
                return true;
            };

            if (hasPrecompiledVertex && !loadPrecompiled(desc->vertexShader, "gpu_precompiled_vs", "vertexMain")) {
                if (device->lastError.empty()) device->lastError = "graphics pipeline could not load precompiled vertex module/entry";
                return GPU_ERROR_INVALID_PARAMETER;
            }
            if (hasPrecompiledFragment && !loadPrecompiled(desc->fragmentShader, "gpu_precompiled_fs", "fragmentMain")) {
                if (device->lastError.empty()) device->lastError = "graphics pipeline could not load precompiled fragment module/entry";
                return GPU_ERROR_INVALID_PARAMETER;
            }

            rhi::ComPtr<slang::IComponentType> globalScope;
            rhi::ComPtr<slang::IBlob> linkDiagnostics;
            if (SLANG_FAILED(slangSession->createCompositeComponentType(
                    moduleComponents.data(), (uint32_t)moduleComponents.size(),
                    globalScope.writeRef(), linkDiagnostics.writeRef()))) {
                device->lastError = "graphics pipeline could not compose precompiled modules";
                return GPU_ERROR_INVALID_PARAMETER;
            }

            std::vector<slang::IComponentType*> rawEntries;
            for (auto& entry : entryPoints) rawEntries.push_back(entry.get());
            rhi::ShaderProgramDesc programDesc = {};
            programDesc.slangGlobalScope = globalScope.get();
            programDesc.slangEntryPoints = rawEntries.data();
            programDesc.slangEntryPointCount = (uint32_t)rawEntries.size();
            programDesc.precompiledEntryPointCode = precompiledCode.data();
            programDesc.precompiledEntryPointCodeCount = (uint32_t)precompiledCode.size();
            rhi::ComPtr<ISlangBlob> programDiagnostics;
            const rhi::Result programResult = device->rhiDevice->createShaderProgram(
                programDesc, rhiProgram.writeRef(), programDiagnostics.writeRef());
            if (SLANG_FAILED(programResult)) {
                device->lastError = "graphics pipeline could not create precompiled shader program (result " +
                    std::to_string((int)programResult) + ")";
                if (programDiagnostics && programDiagnostics->getBufferPointer()) {
                    device->lastError += ": ";
                    device->lastError.append(
                        (const char*)programDiagnostics->getBufferPointer(),
                        programDiagnostics->getBufferSize());
                }
                return GPU_ERROR_INVALID_PARAMETER;
            }
        } else {
        std::string vsSrc((const char*)desc->vertexShader.data, (size_t)desc->vertexShader.size);
        std::string fsSrc((const char*)desc->fragmentShader.data, (size_t)desc->fragmentShader.size);

        std::string vsPath;
        std::string fsPath;
        if (!gpuWriteTextTempFile("gpu_vs.slang", vsSrc.c_str(), vsPath) ||
            !gpuWriteTextTempFile("gpu_fs.slang", fsSrc.c_str(), fsPath)) {
            return GPU_ERROR_INTERNAL;
        }

        std::vector<rhi::ComPtr<slang::IModule>> modules;
        std::vector<rhi::ComPtr<slang::IEntryPoint>> entryPoints;
        std::vector<slang::IComponentType*> componentTypes;

        if (hasVertexShader) {
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
    }

    rhi::RenderPipelineDesc rhiDesc = {};
    rhiDesc.program = rhiProgram;
    rhiDesc.primitiveTopology = convertTopology(desc->primitiveTopology);
    rhiDesc.label = desc->label;

    rhi::ComPtr<rhi::IInputLayout> inputLayout;
    std::vector<rhi::InputElementDesc> inputElements;
    std::vector<rhi::VertexStreamDesc> vertexStreams;
    if (desc->vertexAttributeCount > 0) {
        if (!desc->vertexAttributes || !desc->vertexBindings || desc->vertexBindingCount == 0) {
            device->lastError = "graphics pipeline vertex layout is incomplete";
            return GPU_ERROR_INVALID_PARAMETER;
        }
        inputElements.reserve(desc->vertexAttributeCount);
        for (uint32_t i = 0; i < desc->vertexAttributeCount; ++i) {
            const GpuVertexAttributeDesc& source = desc->vertexAttributes[i];
            if (!source.semanticName || source.binding >= desc->vertexBindingCount) {
                device->lastError = "graphics pipeline vertex attribute is invalid";
                return GPU_ERROR_INVALID_PARAMETER;
            }
            inputElements.push_back({source.semanticName, source.semanticIndex, convertVertexFormat(source.format),
                                     source.offset, source.binding});
        }
        vertexStreams.reserve(desc->vertexBindingCount);
        for (uint32_t i = 0; i < desc->vertexBindingCount; ++i) {
            const GpuVertexBindingDesc& source = desc->vertexBindings[i];
            if (source.binding != i || source.stride == 0) {
                device->lastError = "graphics pipeline vertex stream is invalid";
                return GPU_ERROR_INVALID_PARAMETER;
            }
            vertexStreams.push_back({source.stride,
                source.inputRatePerInstance ? rhi::InputSlotClass::PerInstance : rhi::InputSlotClass::PerVertex,
                source.inputRatePerInstance ? 1u : 0u});
        }
        rhi::InputLayoutDesc inputDesc = {};
        inputDesc.inputElements = inputElements.data(); inputDesc.inputElementCount = (uint32_t)inputElements.size();
        inputDesc.vertexStreams = vertexStreams.data(); inputDesc.vertexStreamCount = (uint32_t)vertexStreams.size();
        if (SLANG_FAILED(device->rhiDevice->createInputLayout(inputDesc, inputLayout.writeRef()))) {
            device->lastError = "graphics pipeline input layout creation failed";
            return GPU_ERROR_INVALID_PARAMETER;
        }
        rhiDesc.inputLayout = inputLayout;
    }

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
    {
        std::lock_guard<std::mutex> lock(device->debugMutex);
        device->lastError.clear();
    }
    rhi::Result r = device->rhiDevice->createRenderPipeline(rhiDesc, rhiPipeline.writeRef());

    if (SLANG_FAILED(r)) {
        std::lock_guard<std::mutex> lock(device->debugMutex);
        if (device->lastError.empty()) {
            device->lastError = "graphics render pipeline creation failed";
        }
        return GPU_ERROR_UNKNOWN;
    }

    uint32_t index = g_renderPipelinePool.allocate(rhiPipeline.detach());
    if (index == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outPipeline->index = index;
    outPipeline->generation = encodeTypeInGeneration(
        GPU_PIPELINE_TYPE_GRAPHICS,
        g_renderPipelinePool.slots[index].generation);

    return GPU_OK;
}

// ============================================================================
// Compute Pipeline Creation
// ============================================================================

extern "C" GpuResult gpuCreateComputePipeline2(GpuDevice device, const GpuComputePipelineDesc2* desc, GpuPipelineHandle* outPipeline) {
    if (!device || !desc || !outPipeline) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    device->lastError.clear();
    const GpuShaderBinary& shader = desc->computeShader;
    if (!shader.data || shader.size == 0) {
        device->lastError = "compute pipeline has no shader data";
        return GPU_ERROR_INVALID_PARAMETER;
    }

    rhi::ComPtr<slang::ISession> slangSession;
    if (SLANG_FAILED(device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
        device->lastError = "compute pipeline could not get Slang session";
        return GPU_ERROR_INTERNAL;
    }

    rhi::ComPtr<slang::IModule> module;
    const bool precompiled = shader.moduleData && shader.moduleSize > 0;
    if (precompiled) {
        const char* moduleName = shader.moduleName ? shader.moduleName : "gpu_precompiled_cs";
        rhi::ComPtr<ISlangBlob> moduleBlob(
            slang_createBlob(shader.moduleData, (size_t)shader.moduleSize));
        if (!moduleBlob) {
            device->lastError = "compute pipeline could not allocate serialized module blob";
            return GPU_ERROR_OUT_OF_MEMORY;
        }
        rhi::ComPtr<ISlangBlob> diagnostics;
        module = slangSession->loadModuleFromIRBlob(
            moduleName, moduleName, moduleBlob, diagnostics.writeRef());
        if (!module) {
            if (diagnostics && diagnostics->getBufferPointer())
                device->lastError.assign(
                    (const char*)diagnostics->getBufferPointer(),
                    diagnostics->getBufferSize());
            else
                device->lastError = "Slang rejected serialized compute module IR";
            return GPU_ERROR_INVALID_PARAMETER;
        }
    } else {
        std::string csSrc((const char*)shader.data, (size_t)shader.size);
        std::string csPath;
        if (!gpuWriteTextTempFile("gpu_cs.slang", csSrc.c_str(), csPath)) {
            device->lastError = "compute pipeline could not materialize source module";
            return GPU_ERROR_INTERNAL;
        }
        slang::IBlob* csDiag = nullptr;
        module = slangSession->loadModule(csPath.c_str(), &csDiag);
        if (!module && csDiag && csDiag->getBufferPointer())
            device->lastError.assign(
                (const char*)csDiag->getBufferPointer(),
                csDiag->getBufferSize());
        if (csDiag) csDiag->release();
        if (!module) {
            if (device->lastError.empty()) device->lastError = "Slang rejected compute source module";
            return GPU_ERROR_INTERNAL;
        }
    }

    rhi::ComPtr<slang::IEntryPoint> csEntry;
    const char* entryName = shader.entryPoint ? shader.entryPoint : "main";
    if (SLANG_FAILED(module->findEntryPointByName(entryName, csEntry.writeRef()))) {
        device->lastError = std::string("compute module has no entry point: ") + entryName;
        return GPU_ERROR_INVALID_ARGS;
    }

    slang::IComponentType* components[] = { module.get(), csEntry.get() };
    rhi::ComPtr<slang::IComponentType> globalScope;
    rhi::ComPtr<slang::IBlob> linkDiag;
    if (SLANG_FAILED(slangSession->createCompositeComponentType(
            components, precompiled ? 1u : 2u, globalScope.writeRef(), linkDiag.writeRef())) ||
        !globalScope) {
        device->lastError = "compute pipeline could not compose shader module";
        return GPU_ERROR_INVALID_PARAMETER;
    }

    rhi::ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = globalScope.get();
    slang::IComponentType* entries[] = { csEntry.get() };
    programDesc.slangEntryPoints = entries;
    programDesc.slangEntryPointCount = 1;
    rhi::ShaderEntryPointCode precompiledCode = {
        shader.data, (size_t)shader.size
    };
    if (precompiled) {
        programDesc.precompiledEntryPointCode = &precompiledCode;
        programDesc.precompiledEntryPointCodeCount = 1;
    }

    rhi::ComPtr<rhi::IShaderProgram> rhiProgram;
    if (SLANG_FAILED(device->rhiDevice->createShaderProgram(programDesc, rhiProgram.writeRef()))) {
        device->lastError = "compute pipeline could not create shader program";
        return GPU_ERROR_INVALID_PARAMETER;
    }

    rhi::ComputePipelineDesc rhiDesc = {};
    rhiDesc.program = rhiProgram;
    rhiDesc.label = desc->label;

    rhi::ComPtr<rhi::IComputePipeline> rhiPipeline;
    rhi::Result r = device->rhiDevice->createComputePipeline(rhiDesc, rhiPipeline.writeRef());

    if (SLANG_FAILED(r)) {
        device->lastError = "compute pipeline state creation failed";
        return GPU_ERROR_UNKNOWN;
    }

    uint32_t index = g_computePipelinePool.allocate(rhiPipeline.detach());
    if (index == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outPipeline->index = index;
    outPipeline->generation = encodeTypeInGeneration(
        GPU_PIPELINE_TYPE_COMPUTE,
        g_computePipelinePool.slots[index].generation);

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
    
    outPipeline->index = index;
    outPipeline->generation = encodeTypeInGeneration(
        GPU_PIPELINE_TYPE_COMPUTE,
        g_computePipelinePool.slots[index].generation);

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
