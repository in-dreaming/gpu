#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"
#include "gpu/core/gpu_pipeline.h"  // Include base pipeline types
#include "gpu/shader/gpu_shader_compiler.h"  // For GpuShaderBinary

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

// ============================================================================
// Pipeline Types (Phase 2 Unified)
// ============================================================================

typedef enum {
    GPU_PIPELINE_TYPE_GRAPHICS   = 0,
    GPU_PIPELINE_TYPE_COMPUTE    = 1,
    GPU_PIPELINE_TYPE_RAYTRACING = 2,
} GpuPipelineType;

// ============================================================================
// Extended Graphics Pipeline Description (Phase 2)
// ============================================================================

// Additional topologies for Phase 2 (extends GpuPrimitiveTopology from gpu_pipeline.h)
typedef enum {
    GPU_PRIMITIVE_TOPOLOGY_LINE_STRIP_EXT = 4,
} GpuPrimitiveTopologyExt;

typedef enum {
    GPU_POLYGON_MODE_FILL  = 0,
    GPU_POLYGON_MODE_LINE  = 1,
    GPU_POLYGON_MODE_POINT = 2,
} GpuPolygonMode;

typedef enum {
    GPU_CULL_MODE_NONE  = 0,
    GPU_CULL_MODE_FRONT = 1,
    GPU_CULL_MODE_BACK  = 2,
    GPU_CULL_MODE_BOTH  = 3,
} GpuCullMode;

typedef enum {
    GPU_FRONT_FACE_COUNTER_CLOCKWISE = 0,
    GPU_FRONT_FACE_CLOCKWISE         = 1,
} GpuFrontFace;

typedef enum {
    GPU_COMPARE_OP_NEVER          = 0,
    GPU_COMPARE_OP_LESS           = 1,
    GPU_COMPARE_OP_EQUAL          = 2,
    GPU_COMPARE_OP_LESS_EQUAL     = 3,
    GPU_COMPARE_OP_GREATER        = 4,
    GPU_COMPARE_OP_NOT_EQUAL      = 5,
    GPU_COMPARE_OP_GREATER_EQUAL  = 6,
    GPU_COMPARE_OP_ALWAYS         = 7,
} GpuCompareOp;

typedef enum {
    GPU_BLEND_FACTOR_ZERO                     = 0,
    GPU_BLEND_FACTOR_ONE                      = 1,
    GPU_BLEND_FACTOR_SRC_COLOR                = 2,
    GPU_BLEND_FACTOR_ONE_MINUS_SRC_COLOR      = 3,
    GPU_BLEND_FACTOR_DST_COLOR                = 4,
    GPU_BLEND_FACTOR_ONE_MINUS_DST_COLOR      = 5,
    GPU_BLEND_FACTOR_SRC_ALPHA                = 6,
    GPU_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA      = 7,
    GPU_BLEND_FACTOR_DST_ALPHA                = 8,
    GPU_BLEND_FACTOR_ONE_MINUS_DST_ALPHA      = 9,
    GPU_BLEND_FACTOR_CONSTANT_COLOR           = 10,
    GPU_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR = 11,
    GPU_BLEND_FACTOR_CONSTANT_ALPHA           = 12,
    GPU_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA = 13,
    GPU_BLEND_FACTOR_SRC_ALPHA_SATURATE       = 14,
    GPU_BLEND_FACTOR_SRC1_COLOR               = 15,
    GPU_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR     = 16,
    GPU_BLEND_FACTOR_SRC1_ALPHA               = 17,
    GPU_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA     = 18,
} GpuBlendFactor;

typedef enum {
    GPU_BLEND_OP_ADD              = 0,
    GPU_BLEND_OP_SUBTRACT         = 1,
    GPU_BLEND_OP_REVERSE_SUBTRACT = 2,
    GPU_BLEND_OP_MIN              = 3,
    GPU_BLEND_OP_MAX              = 4,
} GpuBlendOp;

// Vertex input description
typedef struct {
    uint32_t location;
    uint32_t binding;
    GpuFormat format;
    uint32_t offset;
} GpuVertexAttributeDesc;

typedef struct {
    uint32_t binding;
    uint32_t stride;
    bool inputRatePerInstance;  // false = per-vertex, true = per-instance
} GpuVertexBindingDesc;

typedef struct {
    bool blendEnable;
    GpuBlendFactor srcColorBlendFactor;
    GpuBlendFactor dstColorBlendFactor;
    GpuBlendOp colorBlendOp;
    GpuBlendFactor srcAlphaBlendFactor;
    GpuBlendFactor dstAlphaBlendFactor;
    GpuBlendOp alphaBlendOp;
    uint8_t colorWriteMask;  // RGBA bits
} GpuBlendState;

// Render target format with blend state
typedef struct {
    GpuFormat format;
    GpuBlendState blend;
} GpuColorTargetState;

// Extended graphics pipeline descriptor
typedef struct {
    const char* label;
    
    // Shader binaries (Phase 2 - for shader cache integration)
    // If these are set, they take precedence over the program field
    GpuShaderBinary vertexShader;
    GpuShaderBinary fragmentShader;
    GpuShaderBinary geometryShader;
    GpuShaderBinary hullShader;
    GpuShaderBinary domainShader;
    
    // Vertex input
    GpuVertexBindingDesc* vertexBindings;
    uint32_t vertexBindingCount;
    GpuVertexAttributeDesc* vertexAttributes;
    uint32_t vertexAttributeCount;
    
    // Primitive assembly
    GpuPrimitiveTopology primitiveTopology;
    GpuPolygonMode polygonMode;
    GpuCullMode cullMode;
    GpuFrontFace frontFace;
    
    // Depth/stencil
    bool depthTestEnable;
    bool depthWriteEnable;
    GpuCompareOp depthCompareOp;
    bool stencilTestEnable;
    
    // Render targets
    GpuColorTargetState* colorTargets;
    uint32_t colorTargetCount;
    GpuFormat depthStencilFormat;
    uint32_t sampleCount;
} GpuGraphicsPipelineDesc;

// ============================================================================
// Extended Compute Pipeline Description (Phase 2)
// ============================================================================

// Extended compute pipeline with binary support
typedef struct {
    const char* label;
    GpuShaderBinary computeShader;
} GpuComputePipelineDesc2;

// Unified pipeline description (for cache hashing)
typedef struct GpuPipelineDesc {
    GpuPipelineType type;
    union {
        GpuGraphicsPipelineDesc graphics;
        GpuComputePipelineDesc2 compute;
    };
} GpuPipelineDesc;

// ============================================================================
// Unified Pipeline Handle and API
// ============================================================================

// Note: GpuPipelineHandle is defined in gpu/core/gpu_handle.h as GpuHandle

// Unified pipeline creation API
// These extend the Phase 1 gpuCreateRenderPipeline/gpuCreateComputePipeline
// by using the unified GpuPipelineHandle and supporting shader binaries

GpuResult gpuCreateGraphicsPipeline(GpuDevice device, const GpuGraphicsPipelineDesc* desc, GpuPipelineHandle* outPipeline);
GpuResult gpuCreateComputePipeline2(GpuDevice device, const GpuComputePipelineDesc2* desc, GpuPipelineHandle* outPipeline);
GpuResult gpuDestroyPipeline(GpuDevice device, GpuPipelineHandle pipeline);

// Create compute pipeline from a compiled shader program (convenience function)
// This is useful when you have a GpuShaderProgram from gpuCompileShader
struct GpuShaderProgram_t;
typedef struct GpuShaderProgram_t* GpuShaderProgram;
GpuResult gpuCreateComputePipelineFromProgram(GpuDevice device, GpuShaderProgram program, const char* label, GpuPipelineHandle* outPipeline);
GpuResult gpuDestroyPipeline(GpuDevice device, GpuPipelineHandle pipeline);
GpuPipelineType gpuGetPipelineType(GpuDevice device, GpuPipelineHandle pipeline);

#ifdef __cplusplus
}
#endif
