#include "gpu/sync/gpu_fence.h"
#include "gpu/core/gpu_internal.h"

struct GpuFence_t {
    rhi::ComPtr<rhi::IFence> rhiFence;
};

GpuResult gpuCreateFence(GpuDevice device, uint64_t initialValue, GpuFence* outFence)
{
    if (!device || !outFence) return GPU_ERROR_INVALID_ARGS;

    rhi::FenceDesc desc = {};
    desc.initialValue = initialValue;

    rhi::ComPtr<rhi::IFence> rhiFence;
    if (SLANG_FAILED(device->rhiDevice->createFence(desc, rhiFence.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    GpuFence fence = new GpuFence_t();
    fence->rhiFence = rhiFence;
    *outFence = fence;
    return GPU_SUCCESS;
}

uint64_t gpuFenceGetCurrentValue(GpuFence fence)
{
    if (!fence) return 0;
    uint64_t value = 0;
    fence->rhiFence->getCurrentValue(&value);
    return value;
}

GpuResult gpuFenceWait(GpuDevice device, GpuFence fence, uint64_t value, uint32_t timeoutMs)
{
    if (!device || !fence) return GPU_ERROR_INVALID_ARGS;

    rhi::IFence* fences[] = { fence->rhiFence.get() };
    uint64_t values[] = { value };
    uint64_t timeoutNs = (timeoutMs == 0) ? UINT64_MAX : (uint64_t)timeoutMs * 1000000ULL;

    return SLANG_SUCCEEDED(device->rhiDevice->waitForFences(1, fences, values, true, timeoutNs))
        ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}

void gpuDestroyFence(GpuDevice device, GpuFence fence)
{
    (void)device;
    if (!fence) return;
    delete fence;
}

GpuResult gpuQueueSubmitWithFence(GpuCommandQueue queue, uint32_t cmdCount,
                                   GpuCommandBuffer* cmds, GpuFence signalFence,
                                   uint64_t signalValue)
{
    if (!queue || !cmds) return GPU_ERROR_INVALID_ARGS;

    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);

    rhi::ICommandBuffer* rhiBuffers[64];
    GpuCommandBuffer_t* wrappers[64];
    uint32_t count = cmdCount > 64 ? 64 : cmdCount;

    for (uint32_t i = 0; i < count; i++) {
        if (!cmds[i]) {
            rhiBuffers[i] = nullptr;
            wrappers[i] = nullptr;
            continue;
        }
        GpuCommandBuffer_t* buf = static_cast<GpuCommandBuffer_t*>(cmds[i]);
        rhiBuffers[i] = buf->rhiCmdBuffer.get();
        wrappers[i] = buf;
    }

    rhi::SubmitDesc submitDesc = {};
    submitDesc.commandBuffers = rhiBuffers;
    submitDesc.commandBufferCount = count;

    rhi::IFence* fencePtr = nullptr;
    uint64_t fenceVal = signalValue;
    if (signalFence) {
        fencePtr = signalFence->rhiFence.get();
        submitDesc.signalFences = &fencePtr;
        submitDesc.signalFenceValues = &fenceVal;
        submitDesc.signalFenceCount = 1;
    }

    rhiQueue->submit(submitDesc);

    for (uint32_t i = 0; i < count; i++) {
        if (wrappers[i]) delete wrappers[i];
    }
    return GPU_SUCCESS;
}
