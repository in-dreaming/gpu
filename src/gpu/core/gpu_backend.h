#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

// ============================================================================
// Backend Type (Phase E)
// ============================================================================

typedef enum {
    GPU_BACKEND_DEFAULT  = 0,
    GPU_BACKEND_D3D11    = 1,
    GPU_BACKEND_D3D12    = 2,
    GPU_BACKEND_VULKAN   = 3,
    GPU_BACKEND_METAL    = 4,
    GPU_BACKEND_CPU      = 5,
    GPU_BACKEND_CUDA     = 6,
    GPU_BACKEND_WGPU     = 7,
} GpuBackend;

// Query the backend type of an existing device
GpuBackend gpuGetBackendType(GpuDevice device);

// Query the backend API name (e.g. "D3D12", "Vulkan", "Metal")
const char* gpuGetBackendName(GpuDevice device);

// Query the adapter name (e.g. "NVIDIA GeForce RTX 4070")
const char* gpuGetAdapterName(GpuDevice device);

// Query the timestamp frequency for timestamp queries
uint64_t gpuGetTimestampFrequency(GpuDevice device);

// Convert backend enum to string
const char* gpuBackendToString(GpuBackend backend);

#ifdef __cplusplus
}
#endif
