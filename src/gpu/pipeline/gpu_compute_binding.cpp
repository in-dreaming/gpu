#include "gpu/pipeline/gpu_compute_binding.h"
#include "gpu/core/gpu_internal.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

struct GpuComputeBinding_t {
    GpuDevice device;
    GpuComputePipeline pipeline;
};

GpuResult gpuCreateComputeBinding(GpuDevice device, GpuComputePipeline pipeline, GpuComputeBinding* outBinding)
{
    if (!device || !pipeline || !outBinding) return GPU_ERROR_INVALID_ARGS;
    if (!pipeline->rhiPipeline) return GPU_ERROR_INVALID_ARGS;

    auto* binding = new GpuComputeBinding_t{};
    binding->device = device;
    binding->pipeline = pipeline;
    *outBinding = binding;
    return GPU_SUCCESS;
}

void gpuDestroyComputeBinding(GpuComputeBinding binding)
{
    delete binding;
}

GpuResult gpuComputeBindingSetBuffer(GpuComputeBinding binding, const char* fieldName, GpuBufferHandle buffer)
{
    (void)binding;
    (void)fieldName;
    (void)buffer;
    return GPU_SUCCESS;
}

void gpuCmdBindComputeBinding(GpuComputePassEncoder pass, GpuComputePipeline pipeline, GpuComputeBinding binding)
{
    (void)pass;
    (void)pipeline;
    (void)binding;
}

GpuResult gpuComputeBindingDispatch(GpuComputeBinding binding, GpuComputePassEncoder pass,
                                    const char* bufferFieldName, GpuBufferHandle buffer,
                                    uint32_t x, uint32_t y, uint32_t z)
{
    if (!binding || !pass || !bufferFieldName || buffer.index == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* rhiBuf = binding->device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!rhiBuf) return GPU_ERROR_INVALID_ARGS;

    auto* rhiPass = reinterpret_cast<rhi::IComputePassEncoder*>(pass);
    rhi::IShaderObject* root = rhiPass->bindPipeline(binding->pipeline->rhiPipeline.get());
    if (!root) return GPU_ERROR_INTERNAL;

    rhi::ShaderCursor cursor(root);
    rhi::ShaderCursor field = cursor[bufferFieldName];
    if (!field.isValid()) return GPU_ERROR_INVALID_ARGS;
    if (SLANG_FAILED(field.setBinding(rhi::Binding(rhiBuf)))) return GPU_ERROR_INTERNAL;

    rhiPass->dispatchCompute(x, y, z);
    return GPU_SUCCESS;
}
