#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t index;
    uint32_t generation;
} GpuHandle;

typedef GpuHandle GpuBufferHandle;
typedef GpuHandle GpuTextureHandle;
typedef GpuHandle GpuShaderObjectHandle;
typedef GpuHandle GpuPipelineHandle;

#ifdef __cplusplus
#define GPU_NULL_HANDLE GpuHandle{0, 0}
#else
#define GPU_NULL_HANDLE ((GpuHandle){0, 0})
#endif

static inline bool gpuHandleIsValid(GpuHandle h)
{
    return h.index != 0 || h.generation != 0;
}

#ifdef __cplusplus
}
#endif
