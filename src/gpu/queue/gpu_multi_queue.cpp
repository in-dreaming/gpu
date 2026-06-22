#include "gpu/queue/gpu_multi_queue.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/sync/gpu_fence.h"
#include <slang-rhi.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#endif

// ============================================================================
// Queue Set
// ============================================================================

GpuResult gpuGetQueueSet(GpuDevice device, GpuQueueSet* outSet) {
    if (!device || !outSet) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    outSet->graphics = (GpuCommandQueue)device->graphicsQueue.get();
    outSet->compute = (GpuCommandQueue)device->computeQueue.get();
    outSet->transfer = (GpuCommandQueue)device->transferQueue.get();
    
    return GPU_OK;
}

bool gpuQueueIsAvailable(GpuDevice device, GpuQueueType type) {
    if (!device) return false;
    
    switch (type) {
    case GPU_QUEUE_TYPE_GRAPHICS:
        return device->graphicsQueue != nullptr;
    case GPU_QUEUE_TYPE_COMPUTE:
        return device->computeQueue != nullptr;
    case GPU_QUEUE_TYPE_TRANSFER:
        return device->transferQueue != nullptr;
    default:
        return false;
    }
}

// ============================================================================
// Fence Implementation (Extended functions beyond gpu_fence.cpp)
// ============================================================================

// Note: gpuCreateFence, gpuDestroyFence, and gpuFenceGetCurrentValue 
// are implemented in gpu/sync/gpu_fence.cpp

bool gpuFenceIsCompleted(GpuFence fence, uint64_t value) {
    if (!fence) return false;
    
    return gpuFenceGetCurrentValue(fence) >= value;
}

GpuResult gpuFenceWaitOnHost(GpuFence fence, uint64_t value, uint32_t timeoutMs) {
    if (!fence) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    // Poll until fence reaches the specified value
    uint32_t waitedMs = 0;
    const uint32_t pollIntervalMs = 1;
    
    while (gpuFenceGetCurrentValue(fence) < value) {
        if (timeoutMs != 0xFFFFFFFF && waitedMs >= timeoutMs) {
            return GPU_ERROR_TIMEOUT;
        }
        
        // Yield to avoid busy waiting
        #ifdef _WIN32
        Sleep(pollIntervalMs);
        #else
        // On non-Windows, use a short sleep
        struct timespec ts = { 0, pollIntervalMs * 1000000 };
        nanosleep(&ts, nullptr);
        #endif
        
        waitedMs += pollIntervalMs;
    }
    
    return GPU_OK;
}

GpuResult gpuFenceReset(GpuFence fence, uint64_t value) {
    if (!fence) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    if (SLANG_FAILED(fence->rhiFence->setCurrentValue(value))) {
        return GPU_ERROR_INTERNAL;
    }
    return GPU_OK;
}

// ============================================================================
// Enhanced Queue Submission
// ============================================================================

GpuResult gpuQueueSubmitWithSync(GpuCommandQueue queue,
                                  uint32_t waitCount,
                                  const GpuSemaphore* waits,
                                  uint32_t cmdCount,
                                  GpuCommandBuffer* cmds,
                                  const GpuSemaphore* signal) {
    if (!queue || (cmdCount > 0 && !cmds)) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);
    
    // Build submit descriptor
    rhi::SubmitDesc desc = {};
    
    // Command buffers
    rhi::ICommandBuffer* rhiCmds[16] = {};
    uint32_t count = cmdCount < 16 ? cmdCount : 16;
    for (uint32_t i = 0; i < count; i++) {
        if (cmds[i]) {
            GpuCommandBuffer_t* cmdBuf = static_cast<GpuCommandBuffer_t*>(cmds[i]);
            rhiCmds[i] = cmdBuf->rhiCmdBuffer;
        }
    }
    desc.commandBuffers = rhiCmds;
    desc.commandBufferCount = count;
    
    // Wait fences
    rhi::IFence* waitFences[8] = {};
    uint64_t waitValues[8] = {};
    uint32_t numWaits = waitCount < 8 ? waitCount : 8;
    for (uint32_t i = 0; i < numWaits && waits; i++) {
        if (waits[i].fence) {
            // Access the RHI fence from GpuFence wrapper
            GpuFence_t* fenceWrapper = reinterpret_cast<GpuFence_t*>(waits[i].fence);
            if (fenceWrapper && fenceWrapper->rhiFence) {
                waitFences[i] = fenceWrapper->rhiFence.get();
                waitValues[i] = waits[i].value;
            }
        }
    }
    desc.waitFences = waitFences;
    desc.waitFenceValues = waitValues;
    desc.waitFenceCount = numWaits;
    
    // Signal fences
    rhi::IFence* signalFences[8] = {};
    uint64_t signalValues[8] = {};
    if (signal && signal->fence) {
        GpuFence_t* fenceWrapper = reinterpret_cast<GpuFence_t*>(signal->fence);
        if (fenceWrapper && fenceWrapper->rhiFence) {
            signalFences[0] = fenceWrapper->rhiFence.get();
            signalValues[0] = signal->value;
            desc.signalFences = signalFences;
            desc.signalFenceValues = signalValues;
            desc.signalFenceCount = 1;
        }
    }
    
    rhi::Result result = rhiQueue->submit(desc);
    
    return SLANG_SUCCEEDED(result) ? GPU_OK : GPU_ERROR_UNKNOWN;
}

GpuResult gpuQueueSubmitWithWait(GpuCommandQueue queue,
                                  GpuFence waitFence,
                                  uint64_t waitValue,
                                  uint32_t cmdCount,
                                  GpuCommandBuffer* cmds) {
    GpuSemaphore waitSem = { waitFence, waitValue };
    return gpuQueueSubmitWithSync(queue, waitFence ? 1 : 0, &waitSem, cmdCount, cmds, NULL);
}

GpuResult gpuQueueSubmitWithSignal(GpuCommandQueue queue,
                                    uint32_t cmdCount,
                                    GpuCommandBuffer* cmds,
                                    GpuFence signalFence,
                                    uint64_t signalValue) {
    GpuSemaphore signalSem = { signalFence, signalValue };
    return gpuQueueSubmitWithSync(queue, 0, NULL, cmdCount, cmds, signalFence ? &signalSem : NULL);
}

// ============================================================================
// Cross-Queue Synchronization Helpers
// ============================================================================

GpuResult gpuQueueSignal(GpuCommandQueue queue, GpuFence fence, uint64_t value) {
    if (!queue || !fence) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    // Signal is done via submit with signal fence
    GpuSemaphore signal = { fence, value };
    return gpuQueueSubmitWithSync(queue, 0, NULL, 0, NULL, &signal);
}

GpuResult gpuQueueWait(GpuCommandQueue queue, GpuFence fence, uint64_t value) {
    if (!queue || !fence) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    // Wait is done via submit with wait fence
    GpuSemaphore wait = { fence, value };
    return gpuQueueSubmitWithSync(queue, 1, &wait, 0, NULL, NULL);
}

GpuResult gpuSetupComputeToGraphicsSync(GpuCommandQueue computeQueue,
                                         GpuCommandBuffer computeCmd,
                                         GpuCommandQueue graphicsQueue,
                                         GpuCommandBuffer graphicsCmd,
                                         GpuFence fence,
                                         uint64_t value) {
    if (!computeQueue || !graphicsQueue || !fence) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    // Submit compute work with fence signal
    GpuResult result = gpuQueueSubmitWithSignal(computeQueue, computeCmd ? 1 : 0, &computeCmd, fence, value);
    if (result != GPU_OK) return result;
    
    // Submit graphics work waiting on that fence
    result = gpuQueueSubmitWithWait(graphicsQueue, fence, value, graphicsCmd ? 1 : 0, &graphicsCmd);
    
    return result;
}

struct GpuQueryPool_t {
    rhi::ComPtr<rhi::IQueryPool> rhiPool;
};

GpuResult gpuCreateQueryPool(GpuDevice device, uint32_t queryCount, GpuQueryPool* outPool)
{
    if (!device || !outPool || queryCount == 0) return GPU_ERROR_INVALID_PARAMETER;

    rhi::QueryPoolDesc desc = {};
    desc.type = rhi::QueryType::Timestamp;
    desc.count = queryCount;
    desc.label = "timestamp_pool";

    rhi::ComPtr<rhi::IQueryPool> pool;
    if (SLANG_FAILED(device->rhiDevice->createQueryPool(desc, pool.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    GpuQueryPool result = new GpuQueryPool_t();
    result->rhiPool = pool;
    *outPool = result;
    return GPU_OK;
}

void gpuDestroyQueryPool(GpuDevice device, GpuQueryPool pool)
{
    (void)device;
    if (!pool) return;
    delete pool;
}

GpuResult gpuQueryPoolGetResults(GpuQueryPool pool, uint32_t startIndex, uint32_t count, uint64_t* outData)
{
    if (!pool || !outData) return GPU_ERROR_INVALID_PARAMETER;
    return SLANG_SUCCEEDED(pool->rhiPool->getResult(startIndex, count, outData)) ? GPU_OK : GPU_ERROR_INTERNAL;
}

GpuResult gpuQueryPoolReset(GpuQueryPool pool, uint32_t startIndex, uint32_t count)
{
    if (!pool) return GPU_ERROR_INVALID_PARAMETER;
    return SLANG_SUCCEEDED(pool->rhiPool->reset(startIndex, count)) ? GPU_OK : GPU_ERROR_INTERNAL;
}

void gpuCmdWriteTimestamp(GpuCommandBuffer cmd, GpuQueryPool pool, uint32_t queryIndex)
{
    if (!cmd || !pool) return;
    if (cmd->rhiEncoder) {
        cmd->rhiEncoder->writeTimestamp(pool->rhiPool, queryIndex);
    } else if (cmd->inRenderPass && cmd->renderPassEncoder) {
        cmd->renderPassEncoder->writeTimestamp(pool->rhiPool, queryIndex);
    } else if (cmd->inComputePass && cmd->computePassEncoder) {
        cmd->computePassEncoder->writeTimestamp(pool->rhiPool, queryIndex);
    } else if (cmd->inRayTracingPass && cmd->rtPassEncoder) {
        cmd->rtPassEncoder->writeTimestamp(pool->rhiPool, queryIndex);
    }
}



GpuResult gpuQueueWaitIdle(GpuCommandQueue queue) {
    if (!queue) {
        return GPU_ERROR_INVALID_PARAMETER;
    }
    
    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);
    rhiQueue->waitOnHost();
    
    return GPU_OK;
}

GpuResult gpuGetQueueTimestampFrequency(GpuCommandQueue queue, uint64_t* outFrequency) {
    if (!queue || !outFrequency) {
        return GPU_ERROR_INVALID_PARAMETER;
    }

    rhi::ICommandQueue* rhiQueue = reinterpret_cast<rhi::ICommandQueue*>(queue);

    // Try to get timestamp calibration from the queue (real GPU frequency)
    rhi::TimestampCalibration calib = {};
    if (SLANG_SUCCEEDED(rhiQueue->getTimestampCalibration(&calib)) && calib.gpuFrequency > 0) {
        *outFrequency = calib.gpuFrequency;
        return GPU_OK;
    }

    // Fallback: assume nanosecond timestamps (1 GHz)
    *outFrequency = 1000000000ULL;
    return GPU_OK;
}
