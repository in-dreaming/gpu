#include "gpu/workgraph/gpu_work_graph.h"

// TODO: STUB - pending D3D12 Work Graph implementation
GpuResult gpuCreateWorkGraph(GpuDevice device, const GpuWorkGraphDesc* desc, GpuWorkGraph* outGraph)
{
    (void)device;
    (void)desc;

    if (!desc || !outGraph) return GPU_ERROR_INVALID_ARGS;

    *outGraph = NULL;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending D3D12 Work Graph implementation
GpuResult gpuWorkGraphInitialize(GpuWorkGraph graph, GpuCommandBuffer cmd)
{
    (void)graph;
    (void)cmd;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending D3D12 Work Graph implementation
void gpuCmdDispatchWorkGraph(GpuCommandBuffer cmd, GpuWorkGraph graph, uint32_t recordCount)
{
    (void)cmd;
    (void)graph;
    (void)recordCount;
}

// TODO: STUB - pending D3D12 Work Graph implementation
void gpuDestroyWorkGraph(GpuDevice device, GpuWorkGraph graph)
{
    (void)device;
    (void)graph;
}
