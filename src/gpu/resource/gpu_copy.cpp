#include "gpu/resource/gpu_copy.h"
#include "gpu/core/gpu_internal.h"

GpuResult gpuCmdCopyBuffer(GpuCommandEncoder encoder,
                           GpuBufferHandle dst, uint64_t dstOffset,
                           GpuBufferHandle src, uint64_t srcOffset,
                           uint64_t size)
{
    if (!encoder || size == 0) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* rhiDst = encoder->device->bufferPool.resolve(dst.index, dst.generation);
    rhi::IBuffer* rhiSrc = encoder->device->bufferPool.resolve(src.index, src.generation);
    if (!rhiDst || !rhiSrc) return GPU_ERROR_INVALID_ARGS;

    encoder->rhiEncoder->setBufferState(rhiSrc, rhi::ResourceState::CopySource);
    encoder->rhiEncoder->setBufferState(rhiDst, rhi::ResourceState::CopyDestination);
    encoder->rhiEncoder->globalBarrier();
    encoder->rhiEncoder->copyBuffer(rhiDst, dstOffset, rhiSrc, srcOffset, size);

    encoder->device->bufferStates[src.index] = GPU_RESOURCE_STATE_COPY_SOURCE;
    encoder->device->bufferStates[dst.index] = GPU_RESOURCE_STATE_COPY_DEST;
    return GPU_SUCCESS;
}
