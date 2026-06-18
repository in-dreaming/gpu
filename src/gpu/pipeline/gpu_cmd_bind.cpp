#include "gpu/pipeline/gpu_cmd_bind.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/core/gpu_command.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include <slang-rhi.h>
#include <string.h>

#define PIPELINE_TYPE_BITS 2
#define PIPELINE_TYPE_MASK ((1 << PIPELINE_TYPE_BITS) - 1)

static inline GpuPipelineType decodePipelineType(uint32_t generation) {
    return (GpuPipelineType)(generation & PIPELINE_TYPE_MASK);
}

extern rhi::IRenderPipeline* gpuResolveRenderPipeline(GpuDevice device, GpuPipelineHandle pipeline);
extern rhi::IComputePipeline* gpuResolveComputePipeline(GpuDevice device, GpuPipelineHandle pipeline);

static void ensureComputePass(GpuCommandBuffer_t* buf) {
    if (!buf || buf->inComputePass) return;
    if (!buf->rhiEncoder) return;
    buf->computePassEncoder = buf->rhiEncoder->beginComputePass();
    if (buf->computePassEncoder) {
        buf->inComputePass = true;
        if (buf->boundPipeline.index) {
            rhi::IComputePipeline* pipe = gpuResolveComputePipeline(buf->device, buf->boundPipeline);
            if (pipe) {
                buf->computePassEncoder->bindPipeline(pipe);
            }
        }
    }
}

void gpuCmdBindPipeline(GpuCommandBuffer cmd, GpuPipelineHandle pipeline) {
    if (!cmd || !pipeline.index) return;
    cmd->boundPipeline = pipeline;
    GpuPipelineType type = decodePipelineType(pipeline.generation);
    if (type == GPU_PIPELINE_TYPE_COMPUTE) {
        if (cmd->rhiEncoder && !cmd->inComputePass) {
            ensureComputePass(cmd);
        } else if (cmd->inComputePass && cmd->computePassEncoder) {
            rhi::IComputePipeline* pipe = gpuResolveComputePipeline(cmd->device, pipeline);
            if (pipe) {
                cmd->computePassEncoder->bindPipeline(pipe);
            }
        }
    }
}

void gpuCmdBindVertexBuffer(GpuCommandBuffer cmd, uint32_t /*slot*/, GpuBufferHandle /*buffer*/, uint64_t /*offset*/) {
    if (!cmd) return;
}

void gpuCmdBindVertexBuffers(GpuCommandBuffer cmd,
                              uint32_t startSlot,
                              uint32_t count,
                              const GpuBufferHandle* buffers,
                              const uint64_t* offsets) {
    if (!cmd || !buffers || !offsets) return;
    for (uint32_t i = 0; i < count; i++) {
        gpuCmdBindVertexBuffer(cmd, startSlot + i, buffers[i], offsets[i]);
    }
}

void gpuCmdBindIndexBuffer(GpuCommandBuffer cmd, GpuBufferHandle /*buffer*/, uint64_t /*offset*/, GpuIndexType /*type*/) {
    if (!cmd) return;
}

void gpuCmdDrawDeferred(GpuCommandBuffer cmd,
                         uint32_t /*vertexCount*/,
                         uint32_t /*instanceCount*/,
                         uint32_t /*firstVertex*/,
                         uint32_t /*firstInstance*/) {
    if (!cmd) return;
}

void gpuCmdDrawIndexedDeferred(GpuCommandBuffer cmd,
                                uint32_t /*indexCount*/,
                                uint32_t /*instanceCount*/,
                                uint32_t /*firstIndex*/,
                                int32_t /*vertexOffset*/,
                                uint32_t /*firstInstance*/) {
    if (!cmd) return;
}

void gpuCmdDispatch(GpuCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (!cmd) return;
    ensureComputePass(cmd);
    if (cmd->inComputePass && cmd->computePassEncoder) {
        cmd->computePassEncoder->dispatchCompute(groupCountX, groupCountY, groupCountZ);
    }
}

void gpuCmdDispatchIndirect(GpuCommandBuffer cmd, GpuBufferHandle /*indirectBuffer*/, uint64_t /*offset*/) {
    if (!cmd) return;
    ensureComputePass(cmd);
}

void gpuCmdSetViewportFull(GpuCommandBuffer /*cmd*/, float /*x*/, float /*y*/, float /*width*/, float /*height*/, float /*minDepth*/, float /*maxDepth*/) {
}

void gpuCmdSetScissorFull(GpuCommandBuffer /*cmd*/, int32_t /*x*/, int32_t /*y*/, uint32_t /*width*/, uint32_t /*height*/) {
}

void gpuCmdSetBlendConstants(GpuCommandBuffer cmd, const float /*constants*/[4]) {
    if (!cmd) return;
}

void gpuCmdSetDepthBias(GpuCommandBuffer /*cmd*/, float /*constantBias*/, float /*slopeScaledBias*/, float /*clamp*/) {
}

void gpuCmdBindBuffer(GpuCommandBuffer cmd, uint32_t /*set*/, uint32_t /*binding*/, GpuBufferHandle /*buffer*/, uint64_t /*offset*/, uint64_t /*range*/) {
    if (!cmd) return;
}

void gpuCmdBindTexture(GpuCommandBuffer cmd, uint32_t /*set*/, uint32_t /*binding*/, GpuTextureHandle /*texture*/) {
    if (!cmd) return;
}

void gpuCmdBindSampler(GpuCommandBuffer cmd, uint32_t /*set*/, uint32_t /*binding*/, uint32_t /*samplerIndex*/) {
    if (!cmd) return;
}

void gpuCmdDrawIndirect(GpuCommandBuffer cmd, GpuBufferHandle /*indirectBuffer*/, uint64_t /*offset*/) {
    if (!cmd) return;
}

void gpuCmdDrawIndexedIndirect(GpuCommandBuffer cmd, GpuBufferHandle /*indirectBuffer*/, uint64_t /*offset*/) {
    if (!cmd) return;
}
