#include "gpu/layout/gpu_parameter_block.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/reflection/gpu_type_info.h"
#include <string.h>
#include <stdlib.h>
#include <assert.h>

// Parameter block implementation
struct GpuParameterBlock_t {
    GpuTypeInfo* typeInfo;      // Type information (owns this reference)
    uint8_t* cpuData;           // CPU-side staging buffer
    size_t size;                // Total size in bytes
    GpuBufferHandle gpuBuffer;  // GPU buffer backing this block
    bool dirty;                 // Whether CPU data needs upload
};

// Helper to find field by name in type info
static GpuStructField* findField(GpuTypeInfo* typeInfo, const char* fieldName) {
    if (!typeInfo || !fieldName) return NULL;
    
    if (typeInfo->kind != GPU_TYPE_KIND_STRUCT) return NULL;
    
    for (uint32_t i = 0; i < typeInfo->structInfo.fieldCount; i++) {
        GpuStructField* field = &typeInfo->structInfo.fields[i];
        if (field->name && strcmp(field->name, fieldName) == 0) {
            return field;
        }
    }
    return NULL;
}

// ============================================================================
// Parameter Block Creation/Destruction
// ============================================================================

GpuResult gpuCreateParameterBlock(GpuDevice device, const GpuTypeInfo* typeInfo, GpuParameterBlock* outBlock) {
    if (!device || !typeInfo || !outBlock) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    GpuParameterBlock block = (GpuParameterBlock)malloc(sizeof(struct GpuParameterBlock_t));
    if (!block) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    
    // Allocate CPU staging buffer
    block->cpuData = (uint8_t*)malloc(typeInfo->size);
    if (!block->cpuData) {
        free(block);
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    
    memset(block->cpuData, 0, typeInfo->size);
    block->typeInfo = (GpuTypeInfo*)typeInfo;  // Note: not copying, assuming caller manages lifetime
    block->size = typeInfo->size;
    block->gpuBuffer.index = 0;
    block->gpuBuffer.generation = 0;
    block->dirty = true;
    
    *outBlock = block;
    return GPU_OK;
}

void gpuDestroyParameterBlock(GpuDevice device, GpuParameterBlock block) {
    if (!block) return;
    
    if (gpuHandleIsValid(block->gpuBuffer)) {
        gpuDestroyBuffer(device, block->gpuBuffer);
    }
    
    free(block->cpuData);
    free(block);
}

// ============================================================================
// Parameter Block Data Setters
// ============================================================================

void gpuParameterBlockSetData(GpuParameterBlock block, const char* fieldName, const void* data, size_t size) {
    if (!block || !fieldName || !data) return;
    
    GpuStructField* field = findField(block->typeInfo, fieldName);
    if (!field) return;
    
    size_t copySize = size < field->type->size ? size : field->type->size;
    memcpy(block->cpuData + field->offset, data, copySize);
    block->dirty = true;
}

void gpuParameterBlockSetFloat(GpuParameterBlock block, const char* fieldName, float value) {
    gpuParameterBlockSetData(block, fieldName, &value, sizeof(float));
}

void gpuParameterBlockSetFloat2(GpuParameterBlock block, const char* fieldName, const float* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(float) * 2);
}

void gpuParameterBlockSetFloat3(GpuParameterBlock block, const char* fieldName, const float* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(float) * 3);
}

void gpuParameterBlockSetFloat4(GpuParameterBlock block, const char* fieldName, const float* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(float) * 4);
}

void gpuParameterBlockSetInt(GpuParameterBlock block, const char* fieldName, int32_t value) {
    gpuParameterBlockSetData(block, fieldName, &value, sizeof(int32_t));
}

void gpuParameterBlockSetInt2(GpuParameterBlock block, const char* fieldName, const int32_t* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(int32_t) * 2);
}

void gpuParameterBlockSetInt3(GpuParameterBlock block, const char* fieldName, const int32_t* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(int32_t) * 3);
}

void gpuParameterBlockSetInt4(GpuParameterBlock block, const char* fieldName, const int32_t* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(int32_t) * 4);
}

void gpuParameterBlockSetUInt(GpuParameterBlock block, const char* fieldName, uint32_t value) {
    gpuParameterBlockSetData(block, fieldName, &value, sizeof(uint32_t));
}

void gpuParameterBlockSetBool(GpuParameterBlock block, const char* fieldName, bool value) {
    gpuParameterBlockSetData(block, fieldName, &value, sizeof(bool));
}

void gpuParameterBlockSetFloat2x2(GpuParameterBlock block, const char* fieldName, const float* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(float) * 4);
}

void gpuParameterBlockSetFloat3x3(GpuParameterBlock block, const char* fieldName, const float* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(float) * 9);
}

void gpuParameterBlockSetFloat4x4(GpuParameterBlock block, const char* fieldName, const float* value) {
    if (!value) return;
    gpuParameterBlockSetData(block, fieldName, value, sizeof(float) * 16);
}

// ============================================================================
// Resource Handle Setters (Store bindless indices or handles)
// ============================================================================

void gpuParameterBlockSetBuffer(GpuParameterBlock block, const char* fieldName, GpuBufferHandle buffer) {
    if (!block || !fieldName) return;
    // Store the handle index as a uint32 (for bindless)
    uint32_t bindlessIndex = buffer.index;
    gpuParameterBlockSetData(block, fieldName, &bindlessIndex, sizeof(uint32_t));
}

void gpuParameterBlockSetTexture(GpuParameterBlock block, const char* fieldName, GpuTextureHandle texture) {
    if (!block || !fieldName) return;
    uint32_t bindlessIndex = texture.index;
    gpuParameterBlockSetData(block, fieldName, &bindlessIndex, sizeof(uint32_t));
}

void gpuParameterBlockSetSampler(GpuParameterBlock block, const char* fieldName, uint32_t samplerIndex) {
    gpuParameterBlockSetData(block, fieldName, &samplerIndex, sizeof(uint32_t));
}

void gpuParameterBlockSetBindlessIndex(GpuParameterBlock block, const char* fieldName, uint32_t bindlessIndex) {
    gpuParameterBlockSetData(block, fieldName, &bindlessIndex, sizeof(uint32_t));
}

// ============================================================================
// Nested Parameter Block Support
// ============================================================================

void gpuParameterBlockSetSubBlock(GpuParameterBlock parent, const char* fieldName, GpuParameterBlock child) {
    if (!parent || !fieldName || !child) return;
    
    GpuStructField* field = findField(parent->typeInfo, fieldName);
    if (!field) return;
    
    // Verify that the field is a ParameterBlock type
    if (field->type->kind != GPU_TYPE_KIND_PARAMETER_BLOCK) return;
    
    // Get the child's GPU buffer and store its bindless index
    GpuBufferHandle childBuffer = gpuParameterBlockGetGpuBuffer(child);
    if (!gpuHandleIsValid(childBuffer)) {
        // Child's GPU buffer hasn't been created yet
        return;
    }
    
    uint32_t bindlessIndex = childBuffer.index;
    memcpy(parent->cpuData + field->offset, &bindlessIndex, sizeof(uint32_t));
    parent->dirty = true;
}

const GpuTypeInfo* gpuParameterBlockGetNestedTypeInfo(GpuParameterBlock parent, const char* fieldName) {
    if (!parent || !fieldName) return NULL;
    
    GpuStructField* field = findField(parent->typeInfo, fieldName);
    if (!field) return NULL;
    
    if (field->type->kind != GPU_TYPE_KIND_PARAMETER_BLOCK) return NULL;
    
    return field->type;
}

// ============================================================================
// GPU Buffer Management
// ============================================================================

GpuBufferHandle gpuParameterBlockGetGpuBuffer(GpuParameterBlock block) {
    if (!block) {
        GpuBufferHandle nullHandle = {0, 0};
        return nullHandle;
    }
    return block->gpuBuffer;
}

GpuResult gpuParameterBlockUpload(GpuDevice device, GpuParameterBlock block) {
    if (!device || !block) {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    if (!block->dirty) {
        return GPU_OK;
    }

    if (!gpuHandleIsValid(block->gpuBuffer)) {
        GpuBufferDesc desc = {};
        desc.size = block->size;
        desc.usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER | GPU_BUFFER_USAGE_COPY_DEST;
        GpuResult result = gpuCreateBufferInit(device, &desc, block->cpuData, &block->gpuBuffer);
        if (result != GPU_OK) {
            return result;
        }
        block->dirty = false;
        return GPU_OK;
    }

    GpuResult result = gpuUploadToBuffer(device, block->gpuBuffer, block->cpuData, block->size, 0);
    if (result == GPU_OK) {
        block->dirty = false;
    }

    return result;
}

void* gpuParameterBlockMapCpuMemory(GpuParameterBlock block) {
    if (!block) return NULL;
    return block->cpuData;
}

void gpuParameterBlockUnmapCpuMemory(GpuParameterBlock block) {
    if (!block) return;
    block->dirty = true;
}

// ============================================================================
// Parameter Block Queries
// ============================================================================

const GpuTypeInfo* gpuParameterBlockGetTypeInfo(GpuParameterBlock block) {
    if (!block) return NULL;
    return block->typeInfo;
}

size_t gpuParameterBlockGetSize(GpuParameterBlock block) {
    if (!block) return 0;
    return block->size;
}

bool gpuParameterBlockHasField(GpuParameterBlock block, const char* fieldName) {
    if (!block || !fieldName) return false;
    return findField(block->typeInfo, fieldName) != NULL;
}

size_t gpuParameterBlockGetFieldOffset(GpuParameterBlock block, const char* fieldName) {
    if (!block || !fieldName) return (size_t)-1;
    
    GpuStructField* field = findField(block->typeInfo, fieldName);
    if (!field) return (size_t)-1;
    
    return field->offset;
}
