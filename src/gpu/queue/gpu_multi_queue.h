#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_command.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;
typedef struct GpuCommandQueue_t* GpuCommandQueue;

// ============================================================================
// Queue Set
// ============================================================================

// All available queue types in a device
typedef struct {
    GpuCommandQueue graphics;
    GpuCommandQueue compute;
    GpuCommandQueue transfer;
} GpuQueueSet;

// Get all queues at once (convenience function)
GpuResult gpuGetQueueSet(GpuDevice device, GpuQueueSet* outSet);

// Check if a specific queue type is available
bool gpuQueueIsAvailable(GpuDevice device, GpuQueueType type);

// ============================================================================
// Fence (for GPU-GPU and GPU-CPU synchronization)
// ============================================================================

typedef struct GpuFence_t* GpuFence;

// Create a fence with an initial value
// Fences are monotonic counters - they only increase
GpuResult gpuCreateFence(GpuDevice device, uint64_t initialValue, GpuFence* outFence);

// Destroy a fence
void gpuDestroyFence(GpuDevice device, GpuFence fence);

// Get the last signaled value that has been completed by GPU
uint64_t gpuFenceGetCompletedValue(GpuFence fence);

// Check if a specific value has been reached
bool gpuFenceIsCompleted(GpuFence fence, uint64_t value);

// Wait on host until fence reaches at least the specified value
// timeoutMs: maximum wait time in milliseconds (0 = no wait, just poll, UINT32_MAX = infinite)
// Returns GPU_ERROR_TIMEOUT if timeout reached before completion
GpuResult gpuFenceWaitOnHost(GpuFence fence, uint64_t value, uint32_t timeoutMs);

// Reset fence to a specific value (rarely needed, use with caution)
GpuResult gpuFenceReset(GpuFence fence, uint64_t value);

// ============================================================================
// Queue Semaphore (for cross-queue synchronization)
// ============================================================================

typedef struct {
    GpuFence fence;
    uint64_t value;
} GpuSemaphore;

// ============================================================================
// Enhanced Queue Submission with Synchronization
// ============================================================================

// Submit command buffers with wait and signal semaphores
// 
// waitCount / waits: Semaphores to wait on before execution (can be 0/NULL)
// cmdCount / cmds: Command buffers to execute
// signal: Semaphore to signal after execution (can be NULL)
GpuResult gpuQueueSubmitWithSync(GpuCommandQueue queue,
                                  uint32_t waitCount,
                                  const GpuSemaphore* waits,
                                  uint32_t cmdCount,
                                  GpuCommandBuffer* cmds,
                                  const GpuSemaphore* signal);

// Convenience: Submit with single wait semaphore
GpuResult gpuQueueSubmitWithWait(GpuCommandQueue queue,
                                  GpuFence waitFence,
                                  uint64_t waitValue,
                                  uint32_t cmdCount,
                                  GpuCommandBuffer* cmds);

// Convenience: Submit with single signal semaphore
GpuResult gpuQueueSubmitWithSignal(GpuCommandQueue queue,
                                    uint32_t cmdCount,
                                    GpuCommandBuffer* cmds,
                                    GpuFence signalFence,
                                    uint64_t signalValue);

// ============================================================================
// Cross-Queue Synchronization Helpers
// ============================================================================

// Signal a fence from one queue (for use by another queue)
// This is a lightweight operation that just inserts a signal command
GpuResult gpuQueueSignal(GpuCommandQueue queue, GpuFence fence, uint64_t value);

// Wait for a fence on a queue (for cross-queue synchronization)
// This inserts a wait command in the queue's command stream
GpuResult gpuQueueWait(GpuCommandQueue queue, GpuFence fence, uint64_t value);

// Convenience: Setup async compute -> graphics dependency
// Compute signals fence after execution, graphics waits for it before execution
GpuResult gpuSetupComputeToGraphicsSync(GpuCommandQueue computeQueue,
                                         GpuCommandBuffer computeCmd,
                                         GpuCommandQueue graphicsQueue,
                                         GpuCommandBuffer graphicsCmd,
                                         GpuFence fence,
                                         uint64_t value);

// ============================================================================
// Queue Utilities
// ============================================================================

// Wait for all operations on a queue to complete (blocking)
GpuResult gpuQueueWaitIdle(GpuCommandQueue queue);

// Get queue timestamp frequency (for profiling)
GpuResult gpuGetQueueTimestampFrequency(GpuCommandQueue queue, uint64_t* outFrequency);

// Write timestamp to query set (for GPU timing)
// void gpuCmdWriteTimestamp(GpuCommandBuffer cmd, uint32_t queryIndex); // TODO: Add query set API

#ifdef __cplusplus
}
#endif
