#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/reflection/gpu_type_info.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

// ============================================================================
// Parameter Block Handle
// ============================================================================

typedef struct GpuParameterBlock_t* GpuParameterBlock;

// ============================================================================
// Parameter Block Creation/Destruction
// ============================================================================

// Create a parameter block from type information obtained via reflection
GpuResult gpuCreateParameterBlock(GpuDevice device, const GpuTypeInfo* typeInfo, GpuParameterBlock* outBlock);

// Destroy a parameter block
void gpuDestroyParameterBlock(GpuDevice device, GpuParameterBlock block);

// ============================================================================
// Parameter Block Data Setters (by field name)
// ============================================================================

// Set raw bytes at field offset (for custom types)
void gpuParameterBlockSetData(GpuParameterBlock block, const char* fieldName, const void* data, size_t size);

// Scalar types
void gpuParameterBlockSetFloat(GpuParameterBlock block, const char* fieldName, float value);
void gpuParameterBlockSetFloat2(GpuParameterBlock block, const char* fieldName, const float* value);
void gpuParameterBlockSetFloat3(GpuParameterBlock block, const char* fieldName, const float* value);
void gpuParameterBlockSetFloat4(GpuParameterBlock block, const char* fieldName, const float* value);

void gpuParameterBlockSetInt(GpuParameterBlock block, const char* fieldName, int32_t value);
void gpuParameterBlockSetInt2(GpuParameterBlock block, const char* fieldName, const int32_t* value);
void gpuParameterBlockSetInt3(GpuParameterBlock block, const char* fieldName, const int32_t* value);
void gpuParameterBlockSetInt4(GpuParameterBlock block, const char* fieldName, const int32_t* value);

void gpuParameterBlockSetUInt(GpuParameterBlock block, const char* fieldName, uint32_t value);
void gpuParameterBlockSetBool(GpuParameterBlock block, const char* fieldName, bool value);

// Matrix types (column-major)
void gpuParameterBlockSetFloat2x2(GpuParameterBlock block, const char* fieldName, const float* value);
void gpuParameterBlockSetFloat3x3(GpuParameterBlock block, const char* fieldName, const float* value);
void gpuParameterBlockSetFloat4x4(GpuParameterBlock block, const char* fieldName, const float* value);

// Resource handles
void gpuParameterBlockSetBuffer(GpuParameterBlock block, const char* fieldName, GpuBufferHandle buffer);
void gpuParameterBlockSetTexture(GpuParameterBlock block, const char* fieldName, GpuTextureHandle texture);
void gpuParameterBlockSetSampler(GpuParameterBlock block, const char* fieldName, uint32_t samplerIndex);
void gpuParameterBlockSetBindlessIndex(GpuParameterBlock block, const char* fieldName, uint32_t bindlessIndex);

// ============================================================================
// Nested Parameter Block Support
// ============================================================================

// Set a nested parameter block (for ParameterBlock<T> fields)
// The child block's GPU buffer will be bound to the parent's corresponding slot
void gpuParameterBlockSetSubBlock(GpuParameterBlock parent, const char* fieldName, GpuParameterBlock child);

// Get a nested type info by field name (for creating child blocks)
// Returns NULL if the field doesn't exist or isn't a ParameterBlock
const GpuTypeInfo* gpuParameterBlockGetNestedTypeInfo(GpuParameterBlock parent, const char* fieldName);

// ============================================================================
// Parameter Block Upload/Binding
// ============================================================================

// Get the GPU buffer backing this parameter block
// Can be bound manually or used with descriptor systems
GpuBufferHandle gpuParameterBlockGetGpuBuffer(GpuParameterBlock block);

// Upload the parameter block data to GPU (called automatically before draw)
GpuResult gpuParameterBlockUpload(GpuDevice device, GpuParameterBlock block);

// Direct GPU memory access (for advanced usage)
void* gpuParameterBlockMapCpuMemory(GpuParameterBlock block);
void gpuParameterBlockUnmapCpuMemory(GpuParameterBlock block);

// ============================================================================
// Parameter Block Queries
// ============================================================================

// Get the type info used to create this block
const GpuTypeInfo* gpuParameterBlockGetTypeInfo(GpuParameterBlock block);

// Get the total size of the parameter block in bytes
size_t gpuParameterBlockGetSize(GpuParameterBlock block);

// Check if a field exists
bool gpuParameterBlockHasField(GpuParameterBlock block, const char* fieldName);

// Get field offset (returns SIZE_MAX if field not found)
size_t gpuParameterBlockGetFieldOffset(GpuParameterBlock block, const char* fieldName);

#ifdef __cplusplus
}
#endif
