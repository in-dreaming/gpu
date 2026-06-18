#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_format.h"
#include "gpu/capability/gpu_capabilities.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuDevice_t* GpuDevice;

// ============================================================================
// Sparse Resource Support Check
// ============================================================================

static inline bool gpuSparseResourceIsSupported(GpuDevice device) {
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    return caps.supportSparseResource;
}

// ============================================================================
// Sparse Texture Tile Coordinates
// ============================================================================

typedef struct {
    GpuTextureHandle texture;
    uint32_t mipLevel;
    uint32_t tileX;
    uint32_t tileY;
    uint32_t tileZ;
} GpuSparseTileCoord;

// ============================================================================
// Sparse Resource Properties
// ============================================================================

typedef struct {
    uint32_t tileWidth;     // Width of a tile in texels
    uint32_t tileHeight;    // Height of a tile in texels
    uint32_t tileDepth;     // Depth of a tile (for 3D textures)
    uint32_t tileSizeBytes; // Size of a tile in bytes
    
    // Mip region - tiles cover this many mip levels
    uint32_t mipTailFirstLevel;  // First level in mip tail
    uint32_t mipTailTileCount;   // Number of tiles for mip tail
} GpuSparseTextureProperties;

// Get sparse texture properties for a given format and dimensions
GpuResult gpuGetSparseTextureProperties(GpuDevice device,
                                         GpuFormat format,
                                         uint32_t width,
                                         uint32_t height,
                                         uint32_t depth,
                                         uint32_t mipLevels,
                                         GpuSparseTextureProperties* outProps);

// ============================================================================
// Sparse Resource Usage Flag
// ============================================================================

// Add this to GpuTextureUsageFlags when creating a sparse texture:
// GPU_TEXTURE_USAGE_SPARSE = 0x40

// ============================================================================
// Sparse Resource Operations
// ============================================================================

// Reserve sparse texture memory for the specified tiles
// Must be called before mapping. This allocates the GPU virtual address space.
GpuResult gpuSparseReserve(GpuDevice device, 
                            GpuTextureHandle texture, 
                            uint32_t tileCount, 
                            const GpuSparseTileCoord* tiles);

// Release reserved tiles (unmap if mapped first)
GpuResult gpuSparseRelease(GpuDevice device,
                            GpuTextureHandle texture,
                            uint32_t tileCount,
                            const GpuSparseTileCoord* tiles);

// Map a tile to backing memory (can be buffer or device memory)
// The tile must have been reserved first
GpuResult gpuSparseMap(GpuDevice device, 
                        GpuTextureHandle texture, 
                        const GpuSparseTileCoord* tile, 
                        GpuBufferHandle backingMemory, 
                        uint64_t offset);

// Unmap a tile (releases the backing memory but keeps the reservation)
GpuResult gpuSparseUnmap(GpuDevice device, 
                          GpuTextureHandle texture, 
                          const GpuSparseTileCoord* tile);

// Map multiple tiles at once (more efficient)
GpuResult gpuSparseMapMultiple(GpuDevice device,
                                GpuTextureHandle texture,
                                uint32_t tileCount,
                                const GpuSparseTileCoord* tiles,
                                GpuBufferHandle backingMemory,
                                const uint64_t* offsets);

// ============================================================================
// Sparse Buffer Support
// ============================================================================

typedef struct {
    GpuBufferHandle buffer;
    uint64_t pageOffset;  // In pages
} GpuSparsePageCoord;

// Get sparse buffer properties
GpuResult gpuGetSparseBufferProperties(GpuDevice device,
                                        uint64_t size,
                                        uint32_t* outPageSize,
                                        uint32_t* outPageCount);

// Create a sparse buffer (usage must include GPU_BUFFER_USAGE_SPARSE)
GpuResult gpuCreateSparseBuffer(GpuDevice device,
                                 uint64_t size,
                                 uint32_t usage,
                                 GpuBufferHandle* outBuffer);

// Map sparse buffer pages to backing memory
GpuResult gpuSparseBufferMap(GpuDevice device,
                              GpuBufferHandle sparseBuffer,
                              uint64_t pageIndex,
                              GpuBufferHandle backingMemory,
                              uint64_t offset);

GpuResult gpuSparseBufferUnmap(GpuDevice device,
                                GpuBufferHandle sparseBuffer,
                                uint64_t pageIndex);

// ============================================================================
// Tile Streaming Helpers
// ============================================================================

// Calculate tile coord from texture coordinate
GpuResult gpuSparseCalcTileCoord(GpuTextureHandle texture,
                                  uint32_t mipLevel,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t z,
                                  GpuSparseTileCoord* outCoord);

// Get number of tiles needed for a region
uint32_t gpuSparseCalcTileCount(GpuTextureHandle texture,
                                 uint32_t mipLevel,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t depth);

// ============================================================================
// Statistics
// ============================================================================

typedef struct {
    uint32_t totalTileCount;
    uint32_t residentTileCount;
    uint32_t mappedTileCount;
    uint64_t totalMemoryBytes;
    uint64_t residentMemoryBytes;
} GpuSparseTextureStats;

GpuResult gpuSparseGetTextureStats(GpuDevice device,
                                    GpuTextureHandle texture,
                                    GpuSparseTextureStats* outStats);

#ifdef __cplusplus
}
#endif
