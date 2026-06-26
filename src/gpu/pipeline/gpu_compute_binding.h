#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_pipeline.h"
#include "gpu/core/gpu_command.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuComputeBinding_t* GpuComputeBinding;

GpuResult gpuCreateComputeBinding(GpuDevice device, GpuComputePipeline pipeline, GpuComputeBinding* outBinding);
void gpuDestroyComputeBinding(GpuComputeBinding binding);
GpuResult gpuComputeBindingDispatch(GpuComputeBinding binding, GpuComputePassEncoder pass,
                                    const char* bufferFieldName, GpuBufferHandle buffer,
                                    uint32_t x, uint32_t y, uint32_t z);

#ifdef __cplusplus
}
#endif
