#include "gpu/resource/gpu_sparse_resource.h"
#include "gpu/core/gpu_internal.h"
#include <slang-rhi.h>

// ============================================================================
// Sparse Resource (Header-only stubs - full implementation pending slang-rhi support)
// ============================================================================

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuGetSparseTextureProperties(GpuDevice device,
                                                    GpuFormat format,
                                                    uint32_t width,
                                                    uint32_t height,
                                                    uint32_t depth,
                                                    uint32_t mipLevels,
                                                    GpuSparseTextureProperties* outProps) {
    (void)device;
    (void)format;
    (void)width;
    (void)height;
    (void)depth;
    (void)mipLevels;
    (void)outProps;
    
    // Sparse textures require slang-rhi sparse resource support
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseReserve(GpuDevice device,
                                       GpuTextureHandle texture,
                                       uint32_t tileCount,
                                       const GpuSparseTileCoord* tiles) {
    (void)device;
    (void)texture;
    (void)tileCount;
    (void)tiles;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseRelease(GpuDevice device,
                                       GpuTextureHandle texture,
                                       uint32_t tileCount,
                                       const GpuSparseTileCoord* tiles) {
    (void)device;
    (void)texture;
    (void)tileCount;
    (void)tiles;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseMap(GpuDevice device,
                                   GpuTextureHandle texture,
                                   const GpuSparseTileCoord* tile,
                                   GpuBufferHandle backingMemory,
                                   uint64_t offset) {
    (void)device;
    (void)texture;
    (void)tile;
    (void)backingMemory;
    (void)offset;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseUnmap(GpuDevice device,
                                     GpuTextureHandle texture,
                                     const GpuSparseTileCoord* tile) {
    (void)device;
    (void)texture;
    (void)tile;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseMapMultiple(GpuDevice device,
                                           GpuTextureHandle texture,
                                           uint32_t tileCount,
                                           const GpuSparseTileCoord* tiles,
                                           GpuBufferHandle backingMemory,
                                           const uint64_t* offsets) {
    (void)device;
    (void)texture;
    (void)tileCount;
    (void)tiles;
    (void)backingMemory;
    (void)offsets;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuGetSparseBufferProperties(GpuDevice device,
                                                   uint64_t size,
                                                   uint32_t* outPageSize,
                                                   uint32_t* outPageCount) {
    (void)device;
    (void)size;
    (void)outPageSize;
    (void)outPageCount;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuCreateSparseBuffer(GpuDevice device,
                                            uint64_t size,
                                            uint32_t usage,
                                            GpuBufferHandle* outBuffer) {
    (void)device;
    (void)size;
    (void)usage;
    (void)outBuffer;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseBufferMap(GpuDevice device,
                                         GpuBufferHandle sparseBuffer,
                                         uint64_t pageIndex,
                                         GpuBufferHandle backingMemory,
                                         uint64_t offset) {
    (void)device;
    (void)sparseBuffer;
    (void)pageIndex;
    (void)backingMemory;
    (void)offset;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseBufferUnmap(GpuDevice device,
                                           GpuBufferHandle sparseBuffer,
                                           uint64_t pageIndex) {
    (void)device;
    (void)sparseBuffer;
    (void)pageIndex;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseCalcTileCoord(GpuTextureHandle texture,
                                             uint32_t mipLevel,
                                             uint32_t x,
                                             uint32_t y,
                                             uint32_t z,
                                             GpuSparseTileCoord* outCoord) {
    (void)texture;
    (void)mipLevel;
    (void)x;
    (void)y;
    (void)z;
    (void)outCoord;
    return GPU_ERROR_NOT_SUPPORTED;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" uint32_t gpuSparseCalcTileCount(GpuTextureHandle texture,
                                            uint32_t mipLevel,
                                            uint32_t width,
                                            uint32_t height,
                                            uint32_t depth) {
    (void)texture;
    (void)mipLevel;
    (void)width;
    (void)height;
    (void)depth;
    return 0;
}

// TODO: STUB - pending slang-rhi sparse resource API (Phase 2)
extern "C" GpuResult gpuSparseGetTextureStats(GpuDevice device,
                                               GpuTextureHandle texture,
                                               GpuSparseTextureStats* outStats) {
    (void)device;
    (void)texture;
    (void)outStats;
    return GPU_ERROR_NOT_SUPPORTED;
}
