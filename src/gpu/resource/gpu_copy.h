#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuCommandEncoder_t* GpuCommandEncoder;

GpuResult gpuCmdCopyBuffer(GpuCommandEncoder encoder,
                           GpuBufferHandle dst, uint64_t dstOffset,
                           GpuBufferHandle src, uint64_t srcOffset,
                           uint64_t size);

#ifdef __cplusplus
}
#endif
