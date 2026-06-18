#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/reflection/gpu_type_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;

// GpuPipelineHandle and GpuShaderObjectHandle are defined in gpu/core/gpu_handle.h

// ============================================================================
// Shader Object Description
// ============================================================================

typedef struct {
    GpuPipelineHandle pipeline;         // Pipeline this shader object will be used with
    const char* entryPoint;             // Entry point name (e.g., "vertexMain", "fragmentMain")
    const GpuTypeInfo* typeInfo;        // Type layout information from reflection
    const char* label;                  // Debug label (optional)
} GpuShaderObjectDesc;

// ============================================================================
// Shader Object Creation/Destruction
// ============================================================================

// Create a shader object for binding parameters
// The typeInfo should be obtained from reflection cache for the entry point
GpuResult gpuCreateShaderObject(GpuDevice device, const GpuShaderObjectDesc* desc, GpuShaderObjectHandle* outHandle);

// Destroy a shader object
GpuResult gpuDestroyShaderObject(GpuDevice device, GpuShaderObjectHandle handle);

// ============================================================================
// Shader Object Data Setters (by field name)
// ============================================================================

// Set raw data by field name (field must exist in the type layout)
// Automatically handles offset calculation and type validation
void gpuShaderObjectSetData(GpuShaderObjectHandle handle, const char* fieldName, const void* data, size_t size);

// Set scalar types
void gpuShaderObjectSetFloat(GpuShaderObjectHandle handle, const char* fieldName, float value);
void gpuShaderObjectSetFloat2(GpuShaderObjectHandle handle, const char* fieldName, const float* value);
void gpuShaderObjectSetFloat3(GpuShaderObjectHandle handle, const char* fieldName, const float* value);
void gpuShaderObjectSetFloat4(GpuShaderObjectHandle handle, const char* fieldName, const float* value);
void gpuShaderObjectSetInt(GpuShaderObjectHandle handle, const char* fieldName, int32_t value);
void gpuShaderObjectSetInt2(GpuShaderObjectHandle handle, const char* fieldName, const int32_t* value);
void gpuShaderObjectSetInt3(GpuShaderObjectHandle handle, const char* fieldName, const int32_t* value);
void gpuShaderObjectSetInt4(GpuShaderObjectHandle handle, const char* fieldName, const int32_t* value);
void gpuShaderObjectSetUInt(GpuShaderObjectHandle handle, const char* fieldName, uint32_t value);
void gpuShaderObjectSetBool(GpuShaderObjectHandle handle, const char* fieldName, bool value);

// Set matrix types (column-major layout)
void gpuShaderObjectSetFloat2x2(GpuShaderObjectHandle handle, const char* fieldName, const float* value);
void gpuShaderObjectSetFloat3x3(GpuShaderObjectHandle handle, const char* fieldName, const float* value);
void gpuShaderObjectSetFloat4x4(GpuShaderObjectHandle handle, const char* fieldName, const float* value);

// Set resource handles
void gpuShaderObjectSetBuffer(GpuShaderObjectHandle handle, const char* fieldName, GpuBufferHandle buffer);
void gpuShaderObjectSetTexture(GpuShaderObjectHandle handle, const char* fieldName, GpuTextureHandle texture);
void gpuShaderObjectSetSampler(GpuShaderObjectHandle handle, const char* fieldName, uint32_t samplerIndex);

// Set bindless resource index (for bindless descriptor arrays)
void gpuShaderObjectSetBindlessIndex(GpuShaderObjectHandle handle, const char* fieldName, uint32_t bindlessIndex);

// ============================================================================
// Shader Object Binding
// ============================================================================

// Bind a shader object to a command buffer for rendering/compute
// Must be called after gpuCmdBindPipeline and before draw/dispatch
void gpuCmdBindShaderObject(GpuCommandBuffer cmd, GpuShaderObjectHandle handle);

// Bind shader object to a specific set index (for multi-set layouts)
void gpuCmdBindShaderObjectAt(GpuCommandBuffer cmd, uint32_t setIndex, GpuShaderObjectHandle handle);

// ============================================================================
// Shader Object Queries
// ============================================================================

// Get the GPU buffer backing this shader object (for advanced usage)
GpuBufferHandle gpuShaderObjectGetGpuBuffer(GpuShaderObjectHandle handle);

// Get the type info used to create this shader object
const GpuTypeInfo* gpuShaderObjectGetTypeInfo(GpuShaderObjectHandle handle);

#ifdef __cplusplus
}
#endif
