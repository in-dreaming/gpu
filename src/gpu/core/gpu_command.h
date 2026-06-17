#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuCommandQueue_t* GpuCommandQueue;
typedef struct GpuCommandEncoder_t* GpuCommandEncoder;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;

typedef enum {
    GPU_QUEUE_TYPE_GRAPHICS = 0,
    GPU_QUEUE_TYPE_COMPUTE  = 1,
    GPU_QUEUE_TYPE_TRANSFER = 2,
} GpuQueueType;

typedef struct GpuDevice_t* GpuDevice;

GpuResult gpuGetQueue(GpuDevice device, GpuQueueType type, GpuCommandQueue* outQueue);

GpuCommandEncoder gpuBeginCommandEncoder(GpuCommandQueue queue);
GpuCommandBuffer gpuFinishCommandEncoder(GpuCommandEncoder encoder);

GpuResult gpuQueueSubmit(GpuCommandQueue queue, uint32_t count, GpuCommandBuffer* cmdBuffers);
GpuResult gpuQueueWaitOnHost(GpuCommandQueue queue);

typedef enum {
    GPU_LOAD_OP_LOAD      = 0,
    GPU_LOAD_OP_CLEAR     = 1,
    GPU_LOAD_OP_DONT_CARE = 2,
} GpuLoadOp;

typedef enum {
    GPU_STORE_OP_STORE     = 0,
    GPU_STORE_OP_DONT_CARE = 1,
} GpuStoreOp;

#ifdef __cplusplus
}
#endif
