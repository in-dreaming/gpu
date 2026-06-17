#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GPU_TYPE_KIND_SCALAR,
    GPU_TYPE_KIND_VECTOR,
    GPU_TYPE_KIND_MATRIX,
    GPU_TYPE_KIND_STRUCT,
    GPU_TYPE_KIND_ARRAY,
    GPU_TYPE_KIND_TEXTURE,
    GPU_TYPE_KIND_SAMPLER,
    GPU_TYPE_KIND_BUFFER,
    GPU_TYPE_KIND_PARAMETER_BLOCK,
} GpuTypeKind;

typedef struct GpuStructField {
    char* name;
    uint32_t offset;
    struct GpuTypeInfo* type;
} GpuStructField;

struct GpuTypeInfo {
    GpuTypeKind kind;
    char* name;
    uint32_t size;
    uint32_t alignment;
    uint32_t bindingSlot;
    uint32_t bindingSpace;
    union {
        struct { uint32_t count; struct GpuTypeInfo* element; } array;
        struct { uint32_t fieldCount; GpuStructField* fields; } structInfo;
        struct { uint32_t rowCount; uint32_t colCount; struct GpuTypeInfo* scalarType; } matrix;
        struct { uint32_t count; struct GpuTypeInfo* scalarType; } vector;
    };
};

void gpuTypeInfoDestroy(struct GpuTypeInfo* info);

#ifdef __cplusplus
}
#endif
