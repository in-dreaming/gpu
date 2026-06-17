#include "gpu/reflection/gpu_type_info.h"
#include <cstdlib>

void gpuTypeInfoDestroy(GpuTypeInfo* info)
{
    if (!info) return;

    switch (info->kind) {
    case GPU_TYPE_KIND_ARRAY:
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
