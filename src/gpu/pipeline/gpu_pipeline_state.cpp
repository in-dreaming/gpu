#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/core/gpu_pipeline.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include <string.h>
#include <slang-rhi.h>

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

    // Note: slang-rhi requires IShaderProgram* and IInputLayout* which are created
    // through the shader compilation system. For now, this is a stub that would
    // need proper shader program creation to work end-to-end.
    // TODO: Integrate with shader compiler to create IShaderProgram from shader bytecode

    // Build RHI graphics pipeline state
    rhi::RenderPipelineDesc rhiDesc = {};
    rhiDesc.structType = rhi::StructType::RenderPipelineDesc;

    // Primitive topology
    rhiDesc.primitiveTopology = convertTopology(desc->primitiveTopology);

    // Rasterizer state
    rhiDesc.rasterizer.cullMode = convertCullMode(desc->cullMode);
    rhiDesc.rasterizer.frontFace = (desc->frontFace == GPU_FRONT_FACE_CLOCKWISE) 
                                    ? rhi::FrontFaceMode::Clockwise 
                                    : rhi::FrontFaceMode::CounterClockwise;
    rhiDesc.rasterizer.fillMode = convertFillMode(desc->polygonMode);

    // Depth-stencil state
    rhiDesc.depthStencil.depthTestEnable = desc->depthTestEnable;
    rhiDesc.depthStencil.depthWriteEnable = desc->depthWriteEnable;
    rhiDesc.depthStencil.depthFunc = convertCompareOp(desc->depthCompareOp);
    if (desc->depthStencilFormat != GPU_FORMAT_UNDEFINED) {
        rhiDesc.depthStencil.format = gpuFormatToRhi(desc->depthStencilFormat);
    }

    // Color target states
    rhi::ColorTargetDesc targetDescs[8] = {};
    
    uint32_t targetCount = desc->colorTargetCount < 8 ? desc->colorTargetCount : 8;
    for (uint32_t i = 0; i < targetCount; i++) {
        targetDescs[i].format = gpuFormatToRhi(desc->colorTargets[i].format);
        targetDescs[i].enableBlend = desc->colorTargets[i].blend.blendEnable;
        targetDescs[i].color.srcFactor = convertBlendFactor(desc->colorTargets[i].blend.srcColorBlendFactor);
        targetDescs[i].color.dstFactor = convertBlendFactor(desc->colorTargets[i].blend.dstColorBlendFactor);
        targetDescs[i].color.op = convertBlendOp(desc->colorTargets[i].blend.colorBlendOp);
        targetDescs[i].alpha.srcFactor = convertBlendFactor(desc->colorTargets[i].blend.srcAlphaBlendFactor);
        targetDescs[i].alpha.dstFactor = convertBlendFactor(desc->colorTargets[i].blend.dstAlphaBlendFactor);
        targetDescs[i].alpha.op = convertBlendOp(desc->colorTargets[i].blend.alphaBlendOp);
        targetDescs[i].writeMask = (rhi::RenderTargetWriteMask)(desc->colorTargets[i].blend.colorWriteMask & 0xF);
    }
    
    rhiDesc.targets = targetDescs;
    rhiDesc.targetCount = targetCount;

    // Multisample state
    rhiDesc.multisample.sampleCount = desc->sampleCount > 0 ? desc->sampleCount : 1;

    // Label
    rhiDesc.label = desc->label;

    // TODO: Create IShaderProgram from vertexShader and fragmentShader
    // TODO: Create IInputLayout from vertexAttributes and vertexBindings
    // These require integration with the shader compiler system
    
    (void)desc->vertexShader;
    (void)desc->fragmentShader;
    (void)desc->vertexAttributes;
    (void)desc->vertexAttributeCount;
    (void)desc->vertexBindings;
    (void)desc->vertexBindingCount;

    // Create pipeline
    rhi::ComPtr<rhi::IRenderPipeline> rhiPipeline;
    rhi::Result r = device->rhiDevice->createRenderPipeline(rhiDesc, rhiPipeline.writeRef());
    
    if (SLANG_FAILED(r)) {
        return GPU_ERROR_UNKNOWN;
    }

    // Store in pool and get index
    uint32_t index = g_renderPipelinePool.allocate(rhiPipeline.detach());
    if (index == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    
    // Get base generation from pool
    uint32_t baseGen = 1; // Pool starts at generation 1
    outPipeline->index = index;
    outPipeline->generation = encodeTypeInGeneration(GPU_PIPELINE_TYPE_GRAPHICS, baseGen);

    return GPU_OK;
}

// ============================================================================
// Compute Pipeline Creation
// ============================================================================

extern "C" GpuResult gpuCreateComputePipeline2(GpuDevice device, const GpuComputePipelineDesc2* desc, GpuPipelineHandle* outPipeline) {
    if (!device || !desc || !outPipeline) {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    // Build RHI compute pipeline state
    rhi::ComputePipelineDesc rhiDesc = {};
    rhiDesc.structType = rhi::StructType::ComputePipelineDesc;
    
    // TODO: Create IShaderProgram from compute shader bytecode
    // This requires integration with the shader compiler system
    (void)desc->computeShader;

    rhiDesc.label = desc->label;

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
        return GPU_ERROR_NOT_SUPPORTED;
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
