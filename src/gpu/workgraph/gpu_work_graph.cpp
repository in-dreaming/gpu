#include "gpu/workgraph/gpu_work_graph.h"

GpuResult gpuCreateWorkGraph(GpuDevice device, const GpuWorkGraphDesc* desc, GpuWorkGraph* outGraph)
{
    (void)device;
    (void)desc;

    if (!desc || !outGraph) return GPU_ERROR_INVALID_ARGS;

    *outGraph = NULL;
    return GPU_ERROR_NOT_SUPPORTED;
}

GpuResult gpuWorkGraphInitialize(GpuWorkGraph graph, GpuCommandBuffer cmd)
{
    (void)graph;
    (void)cmd;
    return GPU_ERROR_NOT_SUPPORTED;
}

void gpuCmdDispatchWorkGraph(GpuCommandBuffer cmd, GpuWorkGraph graph, uint32_t recordCount)
{
    (void)cmd;
    (void)graph;
    (void)recordCount;
}

void gpuDestroyWorkGraph(GpuDevice device, GpuWorkGraph graph)
{
    (void)device;
    (void)graph;
}
