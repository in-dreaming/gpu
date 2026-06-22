#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/shader/gpu_shader_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuPipelineLayout_t* GpuPipelineLayout;

// ============================================================================
// Binding Kind (maps to slang::BindingType)
// ============================================================================

typedef enum {
    GPU_BINDING_KIND_UNKNOWN                         = 0,
    GPU_BINDING_KIND_SAMPLER                         = 1,
    GPU_BINDING_KIND_TEXTURE                         = 2,
    GPU_BINDING_KIND_CONSTANT_BUFFER                 = 3,
    GPU_BINDING_KIND_PARAMETER_BLOCK                 = 4,
    GPU_BINDING_KIND_TYPED_BUFFER                    = 5,
    GPU_BINDING_KIND_RAW_BUFFER                      = 6,
    GPU_BINDING_KIND_COMBINED_TEXTURE_SAMPLER        = 7,
    GPU_BINDING_KIND_INPUT_RENDER_TARGET             = 8,
    GPU_BINDING_KIND_INLINE_UNIFORM_DATA             = 9,
    GPU_BINDING_KIND_RAY_TRACING_ACCELERATION_STRUCTURE = 10,
    GPU_BINDING_KIND_VARYING_INPUT                   = 11,
    GPU_BINDING_KIND_VARYING_OUTPUT                  = 12,
    GPU_BINDING_KIND_EXISTENTIAL_VALUE               = 13,
    GPU_BINDING_KIND_PUSH_CONSTANT                   = 14,
} GpuBindingKind;

// ============================================================================
// Shader Stage Flags
// ============================================================================

typedef uint32_t GpuShaderStageFlags;
#define GPU_SHADER_STAGE_NONE      0u
#define GPU_SHADER_STAGE_VERTEX    (1u << 0)
#define GPU_SHADER_STAGE_HULL      (1u << 1)
#define GPU_SHADER_STAGE_DOMAIN    (1u << 2)
#define GPU_SHADER_STAGE_GEOMETRY  (1u << 3)
#define GPU_SHADER_STAGE_FRAGMENT  (1u << 4)
#define GPU_SHADER_STAGE_COMPUTE   (1u << 5)
#define GPU_SHADER_STAGE_RAYGEN    (1u << 6)
#define GPU_SHADER_STAGE_INTERSECTION (1u << 7)
#define GPU_SHADER_STAGE_ANY_HIT   (1u << 8)
#define GPU_SHADER_STAGE_CLOSEST_HIT (1u << 9)
#define GPU_SHADER_STAGE_MISS      (1u << 10)
#define GPU_SHADER_STAGE_CALLABLE  (1u << 11)
#define GPU_SHADER_STAGE_MESH      (1u << 12)
#define GPU_SHADER_STAGE_AMPLIFICATION (1u << 13)
#define GPU_SHADER_STAGE_ALL_GRAPHICS (GPU_SHADER_STAGE_VERTEX | GPU_SHADER_STAGE_HULL | \
    GPU_SHADER_STAGE_DOMAIN | GPU_SHADER_STAGE_GEOMETRY | GPU_SHADER_STAGE_FRAGMENT | \
    GPU_SHADER_STAGE_MESH | GPU_SHADER_STAGE_AMPLIFICATION)
#define GPU_SHADER_STAGE_ALL       0xFFFFFFFFu

// ============================================================================
// Binding Range - a single binding within a descriptor set
// ============================================================================

typedef struct {
    GpuBindingKind kind;
    uint32_t set;               // descriptor set index
    uint32_t binding;           // binding index within the set
    uint32_t count;             // number of descriptors (UINT32_MAX = unbounded)
    uint32_t space;             // register space (D3D) / descriptor set (Vulkan)
    uint32_t uniformSize;       // size in bytes for constant buffers / push constants
    const char* name;           // variable name from reflection (owned by layout)
    GpuShaderStageFlags stageFlags;  // which stages use this binding
    bool writable;              // UAV vs SRV
} GpuBindingRange;

// ============================================================================
// Descriptor Set Layout
// ============================================================================

typedef struct {
    uint32_t set;               // set index
    uint32_t space;             // register space offset
    uint32_t bindingCount;      // number of bindings in this set
    const GpuBindingRange* bindings;  // pointer to binding ranges (owned by layout)
} GpuDescriptorSetLayoutInfo;

// ============================================================================
// Push Constant Range
// ============================================================================

typedef struct {
    uint32_t set;
    uint32_t binding;
    uint32_t offset;            // offset in bytes
    uint32_t size;              // size in bytes
    GpuShaderStageFlags stageFlags;
    const char* name;           // owned by layout
} GpuPushConstantRange;

// ============================================================================
// Entry Point Info
// ============================================================================

typedef enum {
    GPU_STAGE_NONE       = 0,
    GPU_STAGE_VERTEX     = 1,
    GPU_STAGE_HULL       = 2,
    GPU_STAGE_DOMAIN     = 3,
    GPU_STAGE_GEOMETRY   = 4,
    GPU_STAGE_FRAGMENT   = 5,
    GPU_STAGE_COMPUTE    = 6,
    GPU_STAGE_RAYGEN     = 7,
    GPU_STAGE_INTERSECTION = 8,
    GPU_STAGE_ANY_HIT    = 9,
    GPU_STAGE_CLOSEST_HIT = 10,
    GPU_STAGE_MISS       = 11,
    GPU_STAGE_CALLABLE   = 12,
    GPU_STAGE_MESH       = 13,
    GPU_STAGE_AMPLIFICATION = 14,
} GpuShaderStage;

typedef struct {
    const char* name;           // owned by layout
    GpuShaderStage stage;
    uint32_t threadGroupSizeX;
    uint32_t threadGroupSizeY;
    uint32_t threadGroupSizeZ;
    uint32_t parameterCount;    // number of entry-point parameters
} GpuEntryPointInfo;

// ============================================================================
// Pipeline Layout Info - the full reflection result
// ============================================================================

typedef struct {
    uint64_t layoutHash;        // SHA-256 truncated to 64-bit hash of the layout
    uint32_t descriptorSetCount;
    const GpuDescriptorSetLayoutInfo* descriptorSets;
    uint32_t bindingRangeCount;
    const GpuBindingRange* bindingRanges;
    uint32_t pushConstantRangeCount;
    const GpuPushConstantRange* pushConstantRanges;
    uint32_t entryPointCount;
    const GpuEntryPointInfo* entryPoints;
    uint32_t globalConstantBufferSize;
    int32_t bindlessSpaceIndex; // -1 if no bindless resources
} GpuPipelineLayoutInfo;

// ============================================================================
// API
// ============================================================================

// Reflect a compiled shader program to extract pipeline layout
GpuResult gpuReflectPipelineLayout(GpuShaderProgram program, GpuPipelineLayout* outLayout);

// Get layout info (pointers are valid until gpuDestroyPipelineLayout is called)
GpuResult gpuGetPipelineLayoutInfo(GpuPipelineLayout layout, GpuPipelineLayoutInfo* outInfo);

// Destroy pipeline layout
void gpuDestroyPipelineLayout(GpuPipelineLayout layout);

// Serialize pipeline layout to JSON file
GpuResult gpuSerializePipelineLayoutJson(GpuPipelineLayout layout, const char* outputPath);

// Serialize pipeline layout to a string (caller must free with gpuFreePipelineLayoutJson)
GpuResult gpuGetPipelineLayoutJson(GpuPipelineLayout layout, char** outJson);
void gpuFreePipelineLayoutJson(char* json);

// Get the layout hash for pipeline cache integration
uint64_t gpuGetPipelineLayoutHash(GpuPipelineLayout layout);

#ifdef __cplusplus
}
#endif
