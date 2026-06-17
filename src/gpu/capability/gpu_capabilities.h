#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

typedef struct {
    bool supportMeshShader;
    bool supportRayTracing;
    bool supportBindless;
    bool supportWorkGraph;
    bool supportCooperativeMatrix;
    bool supportSparseResource;
    uint32_t maxTextureDimension;
    uint32_t maxBindlessDescriptors;
    uint32_t minUniformBufferAlignment;
} GpuCapabilities;

void gpuGetCapabilities(GpuDevice device, GpuCapabilities* outCaps);
bool gpuIsFeatureSupported(GpuDevice device, uint32_t feature);

#ifdef __cplusplus
}
#endif
