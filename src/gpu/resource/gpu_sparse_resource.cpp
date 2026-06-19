#include "gpu/resource/gpu_sparse_resource.h"
#include "gpu/core/gpu_internal.h"
#include <string.h>
#include <map>
#include <mutex>

struct TileMapping {
    GpuTextureHandle texture;
    uint32_t mipLevel;
    uint32_t tileX;
    uint32_t tileY;
    uint32_t tileZ;
    GpuBufferHandle backingMemory;
    uint64_t offset;
};

struct SparseTextureState {
    uint32_t totalTiles;
    uint32_t mappedTiles;
    uint32_t residentTiles;
    uint64_t residentMemoryBytes;
    std::vector<TileMapping> mappings;
};

struct SparseBufferState {
    uint32_t totalPages;
    uint32_t mappedPages;
    uint64_t mappedMemoryBytes;
    std::vector<TileMapping> pageMappings;
};

static std::map<uint64_t, SparseTextureState> s_sparseTextures;
static std::map<uint64_t, SparseBufferState> s_sparseBuffers;
static std::mutex s_sparseMutex;

GpuResult gpuGetSparseTextureProperties(GpuDevice device,
                                        GpuFormat /*format*/,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t depth,
                                        uint32_t mipLevels,
                                        GpuSparseTextureProperties* outProps)
{
    if (!device || !outProps) return GPU_ERROR_INVALID_ARGS;

    outProps->tileWidth = 64;
    outProps->tileHeight = 64;
    outProps->tileDepth = 1;
    outProps->tileSizeBytes = 64 * 64 * 4;

    uint32_t w = width;
    uint32_t h = height;
    uint32_t d = depth > 0 ? depth : 1;
    uint32_t firstLargeMip = mipLevels > 0 ? mipLevels : 1;
    for (uint32_t mip = 0; mip < (mipLevels > 0 ? mipLevels : 1); mip++) {
        if (w <= outProps->tileWidth && h <= outProps->tileHeight && d <= outProps->tileDepth) {
            firstLargeMip = mip;
            break;
        }
        w >>= 1;
        h >>= 1;
        d >>= 1;
    }
    outProps->mipTailFirstLevel = firstLargeMip;
    outProps->mipTailTileCount = 1;

    return GPU_SUCCESS;
}

GpuResult gpuSparseReserve(GpuDevice device,
                            GpuTextureHandle texture,
                            uint32_t tileCount,
                            const GpuSparseTileCoord* tiles)
{
    if (!device || !gpuHandleIsValid(texture) || !tiles || tileCount == 0) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    uint64_t key = ((uint64_t)texture.index << 32) | texture.generation;
    auto& state = s_sparseTextures[key];
    state.totalTiles += tileCount;

    return GPU_SUCCESS;
}

static void sparseUnmapLocked(GpuDevice device,
                               GpuTextureHandle texture,
                               const GpuSparseTileCoord* tile)
{
    if (!device || !gpuHandleIsValid(texture) || !tile) return;

    uint64_t key = ((uint64_t)texture.index << 32) | texture.generation;
    auto it = s_sparseTextures.find(key);
    if (it != s_sparseTextures.end()) {
        auto& state = it->second;
        uint64_t tileSizeBytes = 64 * 64 * 4;
        if (state.mappedTiles > 0) state.mappedTiles--;
        if (state.residentTiles > 0) state.residentTiles--;
        if (state.residentMemoryBytes >= tileSizeBytes) state.residentMemoryBytes -= tileSizeBytes;
        for (auto mit = state.mappings.begin(); mit != state.mappings.end(); ++mit) {
            if (mit->mipLevel == tile->mipLevel && mit->tileX == tile->tileX &&
                mit->tileY == tile->tileY && mit->tileZ == tile->tileZ) {
                state.mappings.erase(mit);
                break;
            }
        }
    }
}

GpuResult gpuSparseRelease(GpuDevice device,
                            GpuTextureHandle texture,
                            uint32_t tileCount,
                            const GpuSparseTileCoord* tiles)
{
    if (!device || !gpuHandleIsValid(texture) || !tiles || tileCount == 0) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    uint64_t key = ((uint64_t)texture.index << 32) | texture.generation;
    auto it = s_sparseTextures.find(key);
    if (it != s_sparseTextures.end()) {
        auto& state = it->second;
        if (state.totalTiles >= tileCount) state.totalTiles -= tileCount;
        for (uint32_t i = 0; i < tileCount && i < 64; i++) {
            sparseUnmapLocked(device, texture, &tiles[i]);
        }
    }

    return GPU_SUCCESS;
}

GpuResult gpuSparseMap(GpuDevice device,
                       GpuTextureHandle texture,
                       const GpuSparseTileCoord* tile,
                       GpuBufferHandle backingMemory,
                       uint64_t offset)
{
    if (!device || !gpuHandleIsValid(texture) || !tile) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    uint64_t key = ((uint64_t)texture.index << 32) | texture.generation;
    auto& state = s_sparseTextures[key];

    state.mappedTiles++;
    state.residentTiles++;
    uint64_t tileSizeBytes = 64 * 64 * 4;
    state.residentMemoryBytes += tileSizeBytes;

    TileMapping m = {};
    m.texture = texture;
    m.mipLevel = tile->mipLevel;
    m.tileX = tile->tileX;
    m.tileY = tile->tileY;
    m.tileZ = tile->tileZ;
    m.backingMemory = backingMemory;
    m.offset = offset;
    state.mappings.push_back(m);

    if (gpuHandleIsValid(backingMemory)) {
        rhi::IBuffer* srcBuf = device->bufferPool.resolve(backingMemory.index, backingMemory.generation);
        rhi::ITexture* dstTex = device->texturePool.resolve(texture.index, texture.generation);
        if (srcBuf && dstTex) {
        }
    }

    return GPU_SUCCESS;
}

GpuResult gpuSparseUnmap(GpuDevice device,
                          GpuTextureHandle texture,
                          const GpuSparseTileCoord* tile)
{
    if (!device || !gpuHandleIsValid(texture) || !tile) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    sparseUnmapLocked(device, texture, tile);
    return GPU_SUCCESS;
}

GpuResult gpuSparseMapMultiple(GpuDevice device,
                                GpuTextureHandle texture,
                                uint32_t tileCount,
                                const GpuSparseTileCoord* tiles,
                                GpuBufferHandle backingMemory,
                                const uint64_t* offsets)
{
    if (!device || !gpuHandleIsValid(texture) || !tiles || !offsets) return GPU_ERROR_INVALID_ARGS;

    GpuResult result = GPU_SUCCESS;
    for (uint32_t i = 0; i < tileCount; i++) {
        result = gpuSparseMap(device, texture, &tiles[i], backingMemory, offsets[i]);
        if (result != GPU_SUCCESS) break;
    }
    return result;
}

GpuResult gpuGetSparseBufferProperties(GpuDevice device,
                                        uint64_t size,
                                        uint32_t* outPageSize,
                                        uint32_t* outPageCount)
{
    if (!device) return GPU_ERROR_INVALID_ARGS;

    uint32_t pageSize = 65536;
    uint32_t pageCount = (uint32_t)((size + pageSize - 1) / pageSize);

    if (outPageSize) *outPageSize = pageSize;
    if (outPageCount) *outPageCount = pageCount;

    return GPU_SUCCESS;
}

GpuResult gpuCreateSparseBuffer(GpuDevice device,
                                  uint64_t size,
                                  uint32_t usage,
                                  GpuBufferHandle* outBuffer)
{
    if (!device || !outBuffer || size == 0) return GPU_ERROR_INVALID_ARGS;

    GpuBufferDesc desc = {};
    desc.size = size;
    desc.usage = usage | GPU_BUFFER_USAGE_UNORDERED_ACCESS;
    desc.label = "sparse_buffer";

    GpuResult res = gpuCreateBuffer(device, &desc, outBuffer);
    if (res != GPU_SUCCESS) return res;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    uint64_t key = ((uint64_t)outBuffer->index << 32) | outBuffer->generation;
    auto& state = s_sparseBuffers[key];
    state.totalPages = (uint32_t)((size + 65535) / 65536);
    state.mappedPages = 0;
    state.mappedMemoryBytes = 0;

    return GPU_SUCCESS;
}

GpuResult gpuSparseBufferMap(GpuDevice device,
                               GpuBufferHandle sparseBuffer,
                               uint64_t /*pageIndex*/,
                               GpuBufferHandle backingMemory,
                               uint64_t /*offset*/)
{
    if (!device || !gpuHandleIsValid(sparseBuffer) || !gpuHandleIsValid(backingMemory)) return GPU_ERROR_INVALID_ARGS;

    rhi::IBuffer* dstBuf = device->bufferPool.resolve(sparseBuffer.index, sparseBuffer.generation);
    rhi::IBuffer* srcBuf = device->bufferPool.resolve(backingMemory.index, backingMemory.generation);
    if (!dstBuf || !srcBuf) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    uint64_t key = ((uint64_t)sparseBuffer.index << 32) | sparseBuffer.generation;
    auto& state = s_sparseBuffers[key];
    if (state.mappedPages < state.totalPages) {
        state.mappedPages++;
        state.mappedMemoryBytes += 65536;
    }

    return GPU_SUCCESS;
}

GpuResult gpuSparseBufferUnmap(GpuDevice device,
                                 GpuBufferHandle sparseBuffer,
                                 uint64_t /*pageIndex*/)
{
    if (!device || !gpuHandleIsValid(sparseBuffer)) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    uint64_t key = ((uint64_t)sparseBuffer.index << 32) | sparseBuffer.generation;
    auto it = s_sparseBuffers.find(key);
    if (it != s_sparseBuffers.end()) {
        auto& state = it->second;
        if (state.mappedPages > 0) {
            state.mappedPages--;
            if (state.mappedMemoryBytes >= 65536) state.mappedMemoryBytes -= 65536;
        }
    }

    return GPU_SUCCESS;
}

GpuResult gpuSparseCalcTileCoord(GpuTextureHandle texture,
                                  uint32_t mipLevel,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t z,
                                  GpuSparseTileCoord* outCoord)
{
    if (!gpuHandleIsValid(texture) || !outCoord) return GPU_ERROR_INVALID_ARGS;

    uint32_t tileSize = 64;
    outCoord->texture = texture;
    outCoord->mipLevel = mipLevel;
    outCoord->tileX = x / tileSize;
    outCoord->tileY = y / tileSize;
    outCoord->tileZ = z;

    return GPU_SUCCESS;
}

uint32_t gpuSparseCalcTileCount(GpuTextureHandle /*texture*/,
                                uint32_t /*mipLevel*/,
                                uint32_t width,
                                uint32_t height,
                                uint32_t depth)
{
    uint32_t tileSize = 64;
    uint32_t tilesX = (width + tileSize - 1) / tileSize;
    uint32_t tilesY = (height + tileSize - 1) / tileSize;
    uint32_t tilesZ = depth > 0 ? depth : 1;
    return tilesX * tilesY * tilesZ;
}

GpuResult gpuSparseGetTextureStats(GpuDevice device,
                                     GpuTextureHandle texture,
                                     GpuSparseTextureStats* outStats)
{
    if (!device || !gpuHandleIsValid(texture) || !outStats) return GPU_ERROR_INVALID_ARGS;

    memset(outStats, 0, sizeof(GpuSparseTextureStats));

    rhi::ITexture* tex = device->texturePool.resolve(texture.index, texture.generation);
    if (!tex) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(s_sparseMutex);
    uint64_t key = ((uint64_t)texture.index << 32) | texture.generation;
    auto it = s_sparseTextures.find(key);
    if (it != s_sparseTextures.end()) {
        auto& state = it->second;
        outStats->totalTileCount = state.totalTiles;
        outStats->residentTileCount = state.residentTiles;
        outStats->mappedTileCount = state.mappedTiles;
        outStats->totalMemoryBytes = state.totalTiles * 64 * 64 * 4;
        outStats->residentMemoryBytes = state.residentMemoryBytes;
    } else {
        const auto& desc = tex->getDesc().size;
        uint32_t tilesX = (desc.width + 63) / 64;
        uint32_t tilesY = (desc.height + 63) / 64;
        uint32_t total = tilesX * tilesY;
        outStats->totalTileCount = total;
    }

    return GPU_SUCCESS;
}
