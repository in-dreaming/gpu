#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_device.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/core/gpu_command.h"
#include "gpu/core/gpu_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    GpuTensorFormat aFormat;
    GpuTensorFormat bFormat;
    GpuTensorFormat cFormat;
    bool useCooperativeMatrix;
} GpuMatmulDesc;

typedef struct {
    GpuTensorHandle a;
    GpuTensorHandle b;
    GpuTensorHandle c;
} GpuMatmulBindings;

GpuResult gpuCreateMatmulPipeline(GpuDevice device, const GpuMatmulDesc* desc, GpuPipelineHandle* outPipeline);
void gpuCmdMatmul(GpuCommandBuffer cmd, GpuPipelineHandle matmulPipeline, const GpuMatmulBindings* bindings);

#ifdef __cplusplus
}
#endif
