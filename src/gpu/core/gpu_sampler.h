#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef GpuHandle GpuSamplerHandle;

typedef enum {
    GPU_FILTER_NEAREST = 0,
    GPU_FILTER_LINEAR = 1,
} GpuFilter;

typedef enum {
    GPU_SAMPLER_ADDRESS_MODE_REPEAT = 0,
    GPU_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1,
    GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2,
    GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
} GpuSamplerAddressMode;

typedef enum {
    GPU_SAMPLER_REDUCTION_MODE_STANDARD = 0,
    GPU_SAMPLER_REDUCTION_MODE_MIN = 1,
    GPU_SAMPLER_REDUCTION_MODE_MAX = 2,
} GpuSamplerReductionMode;

typedef struct {
    GpuFilter minFilter;
    GpuFilter magFilter;
    GpuFilter mipFilter;
    GpuSamplerAddressMode addressModeU;
    GpuSamplerAddressMode addressModeV;
    GpuSamplerAddressMode addressModeW;
    float minLod;
    float maxLod;
    float lodBias;
    uint32_t maxAnisotropy;
    bool compareEnable;
    uint32_t compareOp;  // GpuCompareOp
    GpuSamplerReductionMode reductionMode;
    float borderColor[4];
    bool unnormalizedCoordinates;
    const char* label;
} GpuSamplerDesc;

GpuResult gpuCreateSampler(GpuDevice device, const GpuSamplerDesc* desc, GpuSamplerHandle* outSampler);
void gpuDestroySampler(GpuDevice device, GpuSamplerHandle sampler);

#ifdef __cplusplus
}
#endif
