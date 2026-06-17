#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuShaderCache_t* GpuShaderCache;

GpuResult gpuShaderCacheOpen(const char* cacheDir, GpuShaderCache* outCache);
GpuResult gpuShaderCacheLookup(GpuShaderCache cache, const char* key, uint8_t** outData, uint64_t* outSize);
GpuResult gpuShaderCacheStore(GpuShaderCache cache, const char* key, const uint8_t* data, uint64_t size);
void gpuShaderCacheClose(GpuShaderCache cache);

#ifdef __cplusplus
}
#endif
