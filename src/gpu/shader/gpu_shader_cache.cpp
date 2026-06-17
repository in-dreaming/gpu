#include "gpu/shader/gpu_shader_cache.h"
#include <fstream>
#include <sstream>
#include <mutex>
#include <map>
#include <vector>
#include <filesystem>

struct GpuShaderCache_t {
    std::string cacheDir;
    std::map<std::string, std::vector<uint8_t>> memoryCache;
    std::mutex mutex;
};

GpuResult gpuShaderCacheOpen(const char* cacheDir, GpuShaderCache* outCache)
{
    if (!cacheDir || !outCache) return GPU_ERROR_INVALID_ARGS;

    std::error_code ec;
    std::filesystem::create_directories(cacheDir, ec);
    if (ec) return GPU_ERROR_INTERNAL;

    GpuShaderCache cache = new GpuShaderCache_t();
    cache->cacheDir = cacheDir;
    *outCache = cache;
    return GPU_SUCCESS;
}

GpuResult gpuShaderCacheLookup(GpuShaderCache cache, const char* key, uint8_t** outData, uint64_t* outSize)
{
    if (!cache || !key || !outData || !outSize) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(cache->mutex);

    auto it = cache->memoryCache.find(key);
    if (it != cache->memoryCache.end()) {
        *outSize = it->second.size();
        *outData = it->second.data();
        return GPU_SUCCESS;
    }

    std::filesystem::path filePath = std::filesystem::path(cache->cacheDir) / key;
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return GPU_ERROR_NOT_SUPPORTED;

    auto size = file.tellg();
    file.seekg(0);
    auto& data = cache->memoryCache[key];
    data.resize(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(data.data()), size);

    *outSize = data.size();
    *outData = data.data();
    return GPU_SUCCESS;
}

GpuResult gpuShaderCacheStore(GpuShaderCache cache, const char* key, const uint8_t* data, uint64_t size)
{
    if (!cache || !key || !data || size == 0) return GPU_ERROR_INVALID_ARGS;

    std::lock_guard<std::mutex> lock(cache->mutex);

    cache->memoryCache[key] = std::vector<uint8_t>(data, data + size);

    std::filesystem::path filePath = std::filesystem::path(cache->cacheDir) / key;
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) return GPU_ERROR_INTERNAL;
    file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));

    return GPU_SUCCESS;
}

void gpuShaderCacheClose(GpuShaderCache cache)
{
    if (!cache) return;
    delete cache;
}
