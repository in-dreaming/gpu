#include "gpu/core/gpu_pipeline.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/core/gpu_internal.h"
#include <vector>

static rhi::PrimitiveTopology gpuTopologyToRhi(GpuPrimitiveTopology topo)
{
    switch (topo) {
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:  return rhi::PrimitiveTopology::TriangleList;
    case GPU_PRIMITIVE_TOPOLOGY_LINE_LIST:      return rhi::PrimitiveTopology::LineList;
    case GPU_PRIMITIVE_TOPOLOGY_POINT_LIST:     return rhi::PrimitiveTopology::PointList;
    case GPU_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP: return rhi::PrimitiveTopology::TriangleStrip;
    default:                                    return rhi::PrimitiveTopology::TriangleList;
    }
}

GpuResult gpuCreateRenderPipeline(GpuDevice device, const GpuRenderPipelineDesc* desc, GpuRenderPipeline* outPipeline)
{
    if (!device || !desc || !desc->program || !outPipeline) return GPU_ERROR_INVALID_ARGS;

    rhi::RenderPipelineDesc rhiDesc = {};
    rhiDesc.program = desc->program->rhiProgram;
    rhiDesc.primitiveTopology = gpuTopologyToRhi(desc->primitiveTopology);
    rhiDesc.label = desc->label;

    std::vector<rhi::ColorTargetDesc> targets;
    for (uint32_t i = 0; i < desc->targetCount; i++) {
        rhi::ColorTargetDesc target = {};
        target.format = gpuFormatToRhi(desc->targets[i].format);
        targets.push_back(target);
    }
    rhiDesc.targets = targets.data();
    rhiDesc.targetCount = desc->targetCount;

    rhi::ComPtr<rhi::IRenderPipeline> rhiPipeline;
    if (SLANG_FAILED(device->rhiDevice->createRenderPipeline(rhiDesc, rhiPipeline.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    GpuRenderPipeline pipeline = new GpuRenderPipeline_t();
    pipeline->rhiPipeline = rhiPipeline;
    *outPipeline = pipeline;
    return GPU_SUCCESS;
}

void gpuDestroyRenderPipeline(GpuDevice device, GpuRenderPipeline pipeline)
{
    (void)device;
    if (!pipeline) return;
    delete pipeline;
}

GpuResult gpuCreateComputePipeline(GpuDevice device, const GpuComputePipelineDesc* desc, GpuComputePipeline* outPipeline)
{
    if (!device || !desc || !desc->program || !outPipeline) return GPU_ERROR_INVALID_ARGS;

    rhi::ComputePipelineDesc rhiDesc = {};
    rhiDesc.program = desc->program->rhiProgram;
    rhiDesc.label = desc->label;

    rhi::ComPtr<rhi::IComputePipeline> rhiPipeline;
    if (SLANG_FAILED(device->rhiDevice->createComputePipeline(rhiDesc, rhiPipeline.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    GpuComputePipeline pipeline = new GpuComputePipeline_t();
    pipeline->rhiPipeline = rhiPipeline;
    *outPipeline = pipeline;
    return GPU_SUCCESS;
}

void gpuDestroyComputePipeline(GpuDevice device, GpuComputePipeline pipeline)
{
    (void)device;
    if (!pipeline) return;
    delete pipeline;
}
