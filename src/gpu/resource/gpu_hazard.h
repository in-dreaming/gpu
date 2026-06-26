#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/resource/gpu_barrier.h"
#include "gpu/core/gpu_command.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t GpuAccessFlags;
#define GPU_ACCESS_NONE              0u
#define GPU_ACCESS_SHADER_READ       (1u << 0)
#define GPU_ACCESS_SHADER_WRITE      (1u << 1)
#define GPU_ACCESS_COLOR_ATTACHMENT  (1u << 2)
#define GPU_ACCESS_DEPTH_READ        (1u << 3)
#define GPU_ACCESS_DEPTH_WRITE       (1u << 4)
#define GPU_ACCESS_COPY_READ         (1u << 5)
#define GPU_ACCESS_COPY_WRITE        (1u << 6)
#define GPU_ACCESS_PRESENT           (1u << 7)

typedef struct {
    uint32_t mipLevel;
    uint32_t mipCount;
    uint32_t arrayLayer;
    uint32_t arrayCount;
} GpuSubresourceRange;

typedef enum {
    GPU_HAZARD_NONE = 0,
    GPU_HAZARD_READ_AFTER_WRITE = 1,
    GPU_HAZARD_WRITE_AFTER_WRITE = 2,
    GPU_HAZARD_WRITE_AFTER_READ = 3,
    GPU_HAZARD_UNINITIALIZED_ACCESS = 4,
} GpuHazardKind;

GpuAccessFlags gpuAccessFlagsForResourceState(GpuResourceState state);
GpuResourceState gpuResourceStateForAccessFlags(GpuAccessFlags access, bool isBuffer, bool isDepthTexture);
bool gpuAccessFlagsIsWrite(GpuAccessFlags access);
bool gpuResourceStateIsUav(GpuResourceState state);
bool gpuHazardNeedsGlobalBarrier(GpuResourceState beforeState, GpuAccessFlags nextAccess);
GpuHazardKind gpuHazardClassify(GpuResourceState currentState, GpuAccessFlags nextAccess, bool hadWriter);
const char* gpuHazardKindName(GpuHazardKind kind);

GpuResult gpuCmdValidateBufferAccess(GpuDevice device, GpuCommandEncoder encoder,
                                     GpuBufferHandle buffer, GpuAccessFlags access);
GpuResult gpuCmdValidateTextureAccess(GpuDevice device, GpuCommandEncoder encoder,
                                      GpuTextureHandle texture, GpuAccessFlags access);

#ifdef __cplusplus
}
#endif
