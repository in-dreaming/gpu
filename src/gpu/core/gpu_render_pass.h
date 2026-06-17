#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuCommandEncoder_t* GpuCommandEncoder;
typedef struct GpuSurfaceTexture_t* GpuSurfaceTexture;

void gpuCmdClearSurfaceTexture(GpuCommandEncoder encoder, GpuSurfaceTexture texture, float r, float g, float b, float a);

#ifdef __cplusplus
}
#endif
