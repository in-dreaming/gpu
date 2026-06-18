#include "gpu/layout/gpu_shader_object.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/reflection/gpu_type_info.h"
#include <string.h>
#include <stdlib.h>

// Shader object implementation
struct GpuShaderObject_t {
    GpuPipelineHandle pipeline;
    char* entryPoint;
    GpuTypeInfo* typeInfo;
    uint8_t* cpuData;
    size_t size;
    GpuBufferHandle gpuBuffer;
    bool dirty;
};

// Storage pool for shader objects
static GpuHandlePool<GpuShaderObject_t> g_shaderObjectPool;

// Helper to find field by name
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
// Shader Object Creation/Destruction
// ============================================================================

GpuResult gpuCreateShaderObject(GpuDevice device, const GpuShaderObjectDesc* desc, GpuShaderObjectHandle* outHandle) {
    if (!device || !desc || !outHandle) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    if (!gpuHandleIsValid(desc->pipeline) || !desc->typeInfo) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    GpuShaderObject_t* obj = (GpuShaderObject_t*)malloc(sizeof(GpuShaderObject_t));
    if (!obj) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    
    obj->pipeline = desc->pipeline;
    obj->entryPoint = desc->entryPoint ? _strdup(desc->entryPoint) : NULL;
    obj->typeInfo = (GpuTypeInfo*)desc->typeInfo;
    obj->size = desc->typeInfo->size;
    obj->cpuData = (uint8_t*)malloc(obj->size);
    obj->gpuBuffer.index = 0;
    obj->gpuBuffer.generation = 0;
    obj->dirty = true;
    
    if (!obj->cpuData) {
        free(obj->entryPoint);
        free(obj);
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    
    memset(obj->cpuData, 0, obj->size);
    
    uint32_t index = g_shaderObjectPool.allocate(obj);
    if (index == 0) {
        free(obj->cpuData);
        free(obj->entryPoint);
        free(obj);
        return GPU_ERROR_OUT_OF_MEMORY;
    }
    
    outHandle->index = index;
    outHandle->generation = 1;  // Starting generation
    
    return GPU_OK;
}

GpuResult gpuDestroyShaderObject(GpuDevice device, GpuShaderObjectHandle handle) {
    if (!gpuHandleIsValid(handle)) {
        return GPU_OK;
    }
    
    GpuShaderObject_t* obj = g_shaderObjectPool.resolve(handle.index, handle.generation);
    if (!obj) {
        return GPU_OK;
    }
    
    if (gpuHandleIsValid(obj->gpuBuffer)) {
        gpuDestroyBuffer(device, obj->gpuBuffer);
    }
    
    free(obj->cpuData);
    free(obj->entryPoint);
    free(obj);
    
    g_shaderObjectPool.release(handle.index, handle.generation);
    return GPU_OK;
}

// ============================================================================
// Shader Object Data Setters
// ============================================================================

void gpuShaderObjectSetData(GpuShaderObjectHandle handle, const char* fieldName, const void* data, size_t size) {
    if (!gpuHandleIsValid(handle) || !fieldName || !data) return;
    
    GpuShaderObject_t* obj = g_shaderObjectPool.resolve(handle.index, handle.generation);
    if (!obj) return;
    
    GpuStructField* field = findField(obj->typeInfo, fieldName);
    if (!field) return;
    
    size_t copySize = size < field->type->size ? size : field->type->size;
    memcpy(obj->cpuData + field->offset, data, copySize);
    obj->dirty = true;
}

void gpuShaderObjectSetFloat(GpuShaderObjectHandle handle, const char* fieldName, float value) {
    gpuShaderObjectSetData(handle, fieldName, &value, sizeof(float));
}

void gpuShaderObjectSetFloat2(GpuShaderObjectHandle handle, const char* fieldName, const float* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(float) * 2);
}

void gpuShaderObjectSetFloat3(GpuShaderObjectHandle handle, const char* fieldName, const float* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(float) * 3);
}

void gpuShaderObjectSetFloat4(GpuShaderObjectHandle handle, const char* fieldName, const float* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(float) * 4);
}

void gpuShaderObjectSetInt(GpuShaderObjectHandle handle, const char* fieldName, int32_t value) {
    gpuShaderObjectSetData(handle, fieldName, &value, sizeof(int32_t));
}

void gpuShaderObjectSetInt2(GpuShaderObjectHandle handle, const char* fieldName, const int32_t* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(int32_t) * 2);
}

void gpuShaderObjectSetInt3(GpuShaderObjectHandle handle, const char* fieldName, const int32_t* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(int32_t) * 3);
}

void gpuShaderObjectSetInt4(GpuShaderObjectHandle handle, const char* fieldName, const int32_t* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(int32_t) * 4);
}

void gpuShaderObjectSetUInt(GpuShaderObjectHandle handle, const char* fieldName, uint32_t value) {
    gpuShaderObjectSetData(handle, fieldName, &value, sizeof(uint32_t));
}

void gpuShaderObjectSetBool(GpuShaderObjectHandle handle, const char* fieldName, bool value) {
    gpuShaderObjectSetData(handle, fieldName, &value, sizeof(bool));
}

void gpuShaderObjectSetFloat2x2(GpuShaderObjectHandle handle, const char* fieldName, const float* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(float) * 4);
}

void gpuShaderObjectSetFloat3x3(GpuShaderObjectHandle handle, const char* fieldName, const float* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(float) * 9);
}

void gpuShaderObjectSetFloat4x4(GpuShaderObjectHandle handle, const char* fieldName, const float* value) {
    if (!value) return;
    gpuShaderObjectSetData(handle, fieldName, value, sizeof(float) * 16);
}

void gpuShaderObjectSetBuffer(GpuShaderObjectHandle handle, const char* fieldName, GpuBufferHandle buffer) {
    if (!gpuHandleIsValid(handle) || !fieldName) return;
    uint32_t bindlessIndex = buffer.index;
    gpuShaderObjectSetData(handle, fieldName, &bindlessIndex, sizeof(uint32_t));
}

void gpuShaderObjectSetTexture(GpuShaderObjectHandle handle, const char* fieldName, GpuTextureHandle texture) {
    if (!gpuHandleIsValid(handle) || !fieldName) return;
    uint32_t bindlessIndex = texture.index;
    gpuShaderObjectSetData(handle, fieldName, &bindlessIndex, sizeof(uint32_t));
}

void gpuShaderObjectSetSampler(GpuShaderObjectHandle handle, const char* fieldName, uint32_t samplerIndex) {
    gpuShaderObjectSetData(handle, fieldName, &samplerIndex, sizeof(uint32_t));
}

void gpuShaderObjectSetBindlessIndex(GpuShaderObjectHandle handle, const char* fieldName, uint32_t bindlessIndex) {
    gpuShaderObjectSetData(handle, fieldName, &bindlessIndex, sizeof(uint32_t));
}

// ============================================================================
// Shader Object Binding
// ============================================================================

void gpuCmdBindShaderObject(GpuCommandBuffer cmd, GpuShaderObjectHandle handle) {
    if (!cmd || !gpuHandleIsValid(handle)) return;
    
    // Would bind to set 0 by default
    gpuCmdBindShaderObjectAt(cmd, 0, handle);
}

void gpuCmdBindShaderObjectAt(GpuCommandBuffer cmd, uint32_t setIndex, GpuShaderObjectHandle handle) {
    if (!cmd || !gpuHandleIsValid(handle)) return;
    
    GpuShaderObject_t* obj = g_shaderObjectPool.resolve(handle.index, handle.generation);
    if (!obj) return;
    
    // This would set up the shader object binding at the specified set index
    // Actual implementation depends on RHI integration
    (void)setIndex;
}

// ============================================================================
// Shader Object Queries
// ============================================================================

GpuBufferHandle gpuShaderObjectGetGpuBuffer(GpuShaderObjectHandle handle) {
    GpuBufferHandle nullHandle = {0, 0};
    if (!gpuHandleIsValid(handle)) return nullHandle;
    
    GpuShaderObject_t* obj = g_shaderObjectPool.resolve(handle.index, handle.generation);
    if (!obj) return nullHandle;
    
    return obj->gpuBuffer;
}

const GpuTypeInfo* gpuShaderObjectGetTypeInfo(GpuShaderObjectHandle handle) {
    if (!gpuHandleIsValid(handle)) return NULL;
    
    GpuShaderObject_t* obj = g_shaderObjectPool.resolve(handle.index, handle.generation);
    if (!obj) return NULL;
    
    return obj->typeInfo;
}
