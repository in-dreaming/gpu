#include "gpu/pipeline/gpu_cmd_bind.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/core/gpu_command.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include <slang-rhi.h>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <string.h>
#include <stdlib.h>

#define PIPELINE_TYPE_BITS 2
#define PIPELINE_TYPE_MASK ((1 << PIPELINE_TYPE_BITS) - 1)

static inline GpuPipelineType decodePipelineType(uint32_t generation) {
    return (GpuPipelineType)(generation & PIPELINE_TYPE_MASK);
}

static inline uint32_t baseGeneration(uint32_t generation) {
    return generation >> PIPELINE_TYPE_BITS;
}

extern rhi::IRenderPipeline* gpuResolveRenderPipeline(GpuDevice device, GpuPipelineHandle pipeline);
extern rhi::IComputePipeline* gpuResolveComputePipeline(GpuDevice device, GpuPipelineHandle pipeline);

static void applyPendingBinds(GpuCommandBuffer_t* buf) {
    if (!buf || !buf->rootShaderObject || buf->pendingBindCount == 0) return;
    rhi::ShaderCursor cursor(buf->rootShaderObject);
    for (uint32_t i = 0; i < buf->pendingBindCount; i++) {
        auto& bind = buf->pendingBinds[i];
        rhi::ShaderCursor field = cursor[bind.set];
        if (!field.isValid()) continue;
        rhi::ShaderCursor sub = field[bind.binding];
        if (!sub.isValid()) continue;
        if (bind.isBuffer) {
            rhi::IBuffer* rhiBuf = buf->device->bufferPool.resolve(bind.buffer.index, bind.buffer.generation);
            if (rhiBuf) {
                rhi::BufferRange range = {bind.bufferOffset, bind.bufferRange > 0 ? bind.bufferRange : rhiBuf->getDesc().size - bind.bufferOffset};
                sub.setBinding(rhi::Binding(rhiBuf, range));
            }
        } else if (bind.isTexture) {
            rhi::ITexture* rhiTex = buf->device->texturePool.resolve(bind.texture.index, bind.texture.generation);
            if (rhiTex) sub.setBinding(rhi::Binding(rhiTex));
        } else if (bind.isSampler) {
            static rhi::ComPtr<rhi::ISampler> s_defaultSampler;
            if (!s_defaultSampler) {
                buf->device->rhiDevice->createSampler(rhi::SamplerDesc{}, s_defaultSampler.writeRef());
            }
            if (s_defaultSampler) sub.setBinding(rhi::Binding(s_defaultSampler));
        }
    }
    buf->pendingBindCount = 0;
}

void ensureComputePass(GpuCommandBuffer_t* buf) {
    if (!buf || buf->inComputePass) return;
    if (!buf->rhiEncoder) return;
    buf->computePassEncoder = buf->rhiEncoder->beginComputePass();
    if (buf->computePassEncoder) {
        buf->inComputePass = true;
        if (buf->boundPipeline.index) {
            rhi::IComputePipeline* pipe = gpuResolveComputePipeline(buf->device, buf->boundPipeline);
            if (pipe) {
                buf->rootShaderObject = buf->computePassEncoder->bindPipeline(pipe);
            }
        }
        applyPendingBinds(buf);
    }
}

void ensureRenderPass(GpuCommandBuffer_t* buf) {
    if (!buf || buf->inRenderPass) return;
    // A graphics pass needs explicit attachments. Creating an empty render pass
    // makes invalid draw streams look successful while producing undefined output.
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
                cmd->rootShaderObject = cmd->computePassEncoder->bindPipeline(pipe);
                applyPendingBinds(cmd);
            }
        }
    } else if (type == GPU_PIPELINE_TYPE_GRAPHICS) {
        if (cmd->inRenderPass && cmd->renderPassEncoder) {
            rhi::IRenderPipeline* pipe = gpuResolveRenderPipeline(cmd->device, pipeline);
            if (pipe) {
                cmd->rootShaderObject = cmd->renderPassEncoder->bindPipeline(pipe);
                applyPendingBinds(cmd);
            }
        }
    }
}

void gpuCmdBindVertexBuffer(GpuCommandBuffer cmd, uint32_t slot, GpuBufferHandle buffer, uint64_t offset) {
    if (!cmd || buffer.index == 0) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::IBuffer* rhiBuf = cmd->device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!rhiBuf) return;
    rhi::RenderState state = {};
    state.vertexBuffers[slot] = rhi::BufferOffsetPair(rhiBuf, offset);
    state.vertexBufferCount = slot + 1;
    cmd->renderPassEncoder->setRenderState(state);
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

void gpuCmdBindIndexBuffer(GpuCommandBuffer cmd, GpuBufferHandle buffer, uint64_t offset, GpuIndexType type) {
    if (!cmd || buffer.index == 0) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::IBuffer* rhiBuf = cmd->device->bufferPool.resolve(buffer.index, buffer.generation);
    if (!rhiBuf) return;
    rhi::RenderState state = {};
    state.indexBuffer = rhi::BufferOffsetPair(rhiBuf, offset);
    state.indexFormat = (type == GPU_INDEX_TYPE_UINT16) ? rhi::IndexFormat::Uint16 : rhi::IndexFormat::Uint32;
    cmd->renderPassEncoder->setRenderState(state);
}

void gpuCmdDrawDeferred(GpuCommandBuffer cmd,
                         uint32_t vertexCount,
                         uint32_t instanceCount,
                         uint32_t firstVertex,
                         uint32_t firstInstance) {
    if (!cmd) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::DrawArguments args = {};
    args.vertexCount = vertexCount;
    args.instanceCount = instanceCount;
    args.startVertexLocation = firstVertex;
    args.startInstanceLocation = firstInstance;
    cmd->renderPassEncoder->draw(args);
}

void gpuCmdDrawIndexedDeferred(GpuCommandBuffer cmd,
                                uint32_t indexCount,
                                uint32_t instanceCount,
                                uint32_t firstIndex,
                                int32_t vertexOffset,
                                uint32_t firstInstance) {
    if (!cmd) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::DrawArguments args = {};
    args.vertexCount = indexCount;
    args.instanceCount = instanceCount;
    args.startIndexLocation = firstIndex;
    args.startVertexLocation = vertexOffset;
    args.startInstanceLocation = firstInstance;
    cmd->renderPassEncoder->drawIndexed(args);
}

void gpuCmdDispatch(GpuCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    if (!cmd) return;
    ensureComputePass(cmd);
    if (cmd->inComputePass && cmd->computePassEncoder) {
        cmd->computePassEncoder->dispatchCompute(groupCountX, groupCountY, groupCountZ);
    }
}

void gpuCmdDispatchIndirect(GpuCommandBuffer cmd, GpuBufferHandle indirectBuffer, uint64_t offset) {
    if (!cmd || indirectBuffer.index == 0) return;
    ensureComputePass(cmd);
    if (!cmd->inComputePass || !cmd->computePassEncoder) return;
    rhi::IBuffer* rhiBuf = cmd->device->bufferPool.resolve(indirectBuffer.index, indirectBuffer.generation);
    if (!rhiBuf) return;
    cmd->computePassEncoder->dispatchComputeIndirect(rhi::BufferOffsetPair(rhiBuf, offset));
}

void gpuCmdSetViewportFull(GpuCommandBuffer cmd, float x, float y, float width, float height, float minDepth, float maxDepth) {
    if (!cmd) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::RenderState state = {};
    state.viewportCount = 1;
    state.viewports[0].originX = x;
    state.viewports[0].originY = y;
    state.viewports[0].extentX = width;
    state.viewports[0].extentY = height;
    state.viewports[0].minZ = minDepth;
    state.viewports[0].maxZ = maxDepth;
    cmd->renderPassEncoder->setRenderState(state);
}

void gpuCmdSetScissorFull(GpuCommandBuffer cmd, int32_t x, int32_t y, uint32_t width, uint32_t height) {
    if (!cmd) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::RenderState state = {};
    state.scissorRectCount = 1;
    state.scissorRects[0].minX = x;
    state.scissorRects[0].minY = y;
    state.scissorRects[0].maxX = x + (int32_t)width;
    state.scissorRects[0].maxY = y + (int32_t)height;
    cmd->renderPassEncoder->setRenderState(state);
}

void gpuCmdSetBlendConstants(GpuCommandBuffer cmd, const float constants[4]) {
    if (!cmd) return;
    memcpy(cmd->blendConstants, constants, sizeof(float) * 4);
    cmd->blendConstantsSet = true;
}

void gpuCmdSetDepthBias(GpuCommandBuffer cmd, float constantBias, float slopeScaledBias, float clamp) {
    if (!cmd) return;
    cmd->depthBiasConstant = constantBias;
    cmd->depthBiasSlopeScaled = slopeScaledBias;
    cmd->depthBiasClamp = clamp;
    cmd->depthBiasSet = true;
}

static uint32_t findOrAddPendingBind(GpuCommandBuffer_t* cmd) {
    if (cmd->pendingBindCount >= 64) return 0xFFFFFFFF;
    return cmd->pendingBindCount++;
}

void gpuCmdBindBuffer(GpuCommandBuffer cmd, uint32_t set, uint32_t binding, GpuBufferHandle buffer, uint64_t offset, uint64_t range) {
    if (!cmd || buffer.index == 0) return;
    uint32_t idx = findOrAddPendingBind(cmd);
    if (idx == 0xFFFFFFFF) return;
    auto& b = cmd->pendingBinds[idx];
    b.set = set;
    b.binding = binding;
    b.buffer = buffer;
    b.bufferOffset = offset;
    b.bufferRange = range;
    b.isBuffer = true;
    b.isTexture = false;
    b.isSampler = false;
    if (cmd->rootShaderObject) applyPendingBinds(cmd);
}

void gpuCmdBindTexture(GpuCommandBuffer cmd, uint32_t set, uint32_t binding, GpuTextureHandle texture) {
    if (!cmd || texture.index == 0) return;
    uint32_t idx = findOrAddPendingBind(cmd);
    if (idx == 0xFFFFFFFF) return;
    auto& b = cmd->pendingBinds[idx];
    b.set = set;
    b.binding = binding;
    b.texture = texture;
    b.isBuffer = false;
    b.isTexture = true;
    b.isSampler = false;
    if (cmd->rootShaderObject) applyPendingBinds(cmd);
}

void gpuCmdBindSampler(GpuCommandBuffer cmd, uint32_t set, uint32_t binding, uint32_t samplerIndex) {
    if (!cmd) return;
    uint32_t idx = findOrAddPendingBind(cmd);
    if (idx == 0xFFFFFFFF) return;
    auto& b = cmd->pendingBinds[idx];
    b.set = set;
    b.binding = binding;
    b.samplerIndex = samplerIndex;
    b.isBuffer = false;
    b.isTexture = false;
    b.isSampler = true;
    if (cmd->rootShaderObject) applyPendingBinds(cmd);
}

void gpuCmdDrawIndirect(GpuCommandBuffer cmd, GpuBufferHandle indirectBuffer, uint64_t offset) {
    if (!cmd || indirectBuffer.index == 0) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::IBuffer* rhiBuf = cmd->device->bufferPool.resolve(indirectBuffer.index, indirectBuffer.generation);
    if (!rhiBuf) return;
    cmd->renderPassEncoder->drawIndirect(1, rhi::BufferOffsetPair(rhiBuf, offset));
}

void gpuCmdDrawIndexedIndirect(GpuCommandBuffer cmd, GpuBufferHandle indirectBuffer, uint64_t offset) {
    if (!cmd || indirectBuffer.index == 0) return;
    ensureRenderPass(cmd);
    if (!cmd->inRenderPass || !cmd->renderPassEncoder) return;
    rhi::IBuffer* rhiBuf = cmd->device->bufferPool.resolve(indirectBuffer.index, indirectBuffer.generation);
    if (!rhiBuf) return;
    cmd->renderPassEncoder->drawIndexedIndirect(1, rhi::BufferOffsetPair(rhiBuf, offset));
}
