#include "gpu/reflection/gpu_type_info.h"
#include <cstdlib>
#include <cstring>

void gpuTypeInfoDestroy(GpuTypeInfo* info)
{
    if (!info) return;

    switch (info->kind) {
    case GPU_TYPE_KIND_ARRAY:
    case GPU_TYPE_KIND_PARAMETER_BLOCK:
        gpuTypeInfoDestroy(info->array.element);
        break;
    case GPU_TYPE_KIND_STRUCT:
        for (uint32_t i = 0; i < info->structInfo.fieldCount; i++) {
            free(info->structInfo.fields[i].name);
            gpuTypeInfoDestroy(info->structInfo.fields[i].type);
        }
        free(info->structInfo.fields);
        break;
    case GPU_TYPE_KIND_MATRIX:
        gpuTypeInfoDestroy(info->matrix.scalarType);
        break;
    case GPU_TYPE_KIND_VECTOR:
        gpuTypeInfoDestroy(info->vector.scalarType);
        break;
    default:
        break;
    }

    free(info->name);
    free(info);
}

GpuTypeInfo* gpuTypeInfoFindField(GpuTypeInfo* root, const char* fieldName)
{
    if (!root || !fieldName || root->kind != GPU_TYPE_KIND_STRUCT) return nullptr;
    for (uint32_t i = 0; i < root->structInfo.fieldCount; i++) {
        const GpuStructField* field = &root->structInfo.fields[i];
        if (field->name && strcmp(field->name, fieldName) == 0) return field->type;
    }
    return nullptr;
}
