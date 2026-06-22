#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

// GpuPipelineDesc is defined in gpu_pipeline_state.h

// ============================================================================
// Pipeline Cache
// ============================================================================

typedef struct GpuPipelineCache_t* GpuPipelineCache;

typedef struct {
    const char* cachePath;      // Path for disk persistence (optional, can be NULL)
    size_t initialDataSize;     // Size of initial pipeline cache data (optional)
    const void* initialData;    // Initial pipeline cache data from previous session (optional)
} GpuPipelineCacheDesc;

// Create/destroy pipeline cache
GpuResult gpuCreatePipelineCache(GpuDevice device, const GpuPipelineCacheDesc* desc, GpuPipelineCache* outCache);
void gpuDestroyPipelineCache(GpuDevice device, GpuPipelineCache cache);

// Get cache data for serialization (call before destroying to save for next session)
GpuResult gpuGetPipelineCacheData(GpuPipelineCache cache, size_t* outSize, void** outData);
void gpuFreePipelineCacheData(void* data);

// ============================================================================
// Pipeline Cache Operations with SHA-256 Keys
// ============================================================================

// SHA-256 hash size in bytes
#define GPU_SHA256_HASH_SIZE 32

// Compute SHA-256 hash of pipeline description for cache key
// Returns a 32-byte hash that uniquely identifies the pipeline state
GpuResult gpuHashPipelineDesc(const GpuPipelineDesc* desc, uint8_t outHash[GPU_SHA256_HASH_SIZE]);

// Compute SHA-256 hash of pipeline description with layout hash for cache key
// This includes the pipeline layout identity to avoid cache collisions
// between pipelines with same shader text but different layouts
GpuResult gpuHashPipelineDescWithLayout(const GpuPipelineDesc* desc, uint64_t layoutHash, uint8_t outHash[GPU_SHA256_HASH_SIZE]);

// Look up a pipeline in the cache by its hash key
// Returns GPU_ERROR_NOT_FOUND if the pipeline is not in the cache
GpuResult gpuPipelineCacheLookup(GpuPipelineCache cache, 
                                  const uint8_t hash[GPU_SHA256_HASH_SIZE], 
                                  GpuPipelineHandle* outPipeline);

// Store a pipeline in the cache with its hash key
GpuResult gpuPipelineCacheStore(GpuPipelineCache cache, 
                                 const uint8_t hash[GPU_SHA256_HASH_SIZE], 
                                 GpuPipelineHandle pipeline);

// Flush cache to disk (if cachePath was provided during creation)
GpuResult gpuPipelineCacheFlush(GpuPipelineCache cache);

// Get cache statistics
void gpuPipelineCacheGetStats(GpuPipelineCache cache, 
                               uint32_t* outHitCount, 
                               uint32_t* outMissCount, 
                               uint32_t* outEntryCount);

#ifdef __cplusplus
}
#endif
