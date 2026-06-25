#include "material_textures.h"
#include "gpu/core/gpu_internal.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <slang-rhi.h>

#include <cstdio>
#include <cstring>
#include <algorithm>
#include <vector>
#include <fstream>
#include <sstream>

static constexpr uint32_t kMaxPathText = 1024;

static int pathJoin(char* out, size_t outSize, const char* root, const char* leaf) {
    size_t rootLen = strlen(root), leafLen = strlen(leaf);
    bool needsSlash = rootLen > 0 && root[rootLen - 1] != '/' && root[rootLen - 1] != '\\';
    size_t totalLen = rootLen + (needsSlash ? 1 : 0) + leafLen;
    if (totalLen + 1 > outSize) return 0;
    memcpy(out, root, rootLen); size_t offset = rootLen;
    if (needsSlash) out[offset++] = '/';
    memcpy(out + offset, leaf, leafLen);
    out[totalLen] = 0; return 1;
}

static bool fileExists(const char* path) { std::ifstream f(path, std::ios::binary); return f.good(); }

static std::string textureFileToken(const std::string& value) {
    std::istringstream ss(value); std::string token, last;
    while (ss >> token) last = token; return last;
}

static std::string baseName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool resolveTexturePath(const char* root, const std::string& materialPath, char* out, size_t outSize) {
    std::string token = textureFileToken(materialPath);
    std::replace(token.begin(), token.end(), '\\', '/');
    if (token.empty()) return false;
    if (pathJoin(out, outSize, root, token.c_str()) && fileExists(out)) return true;
    char texturesDir[kMaxPathText];
    if (pathJoin(texturesDir, sizeof(texturesDir), root, "textures")) {
        std::string name = baseName(token);
        if (pathJoin(out, outSize, texturesDir, name.c_str()) && fileExists(out)) return true;
    }
    return false;
}

static void resizeRgbaNearest(const uint8_t* src, int sw, int sh, uint8_t* dst, uint32_t dw, uint32_t dh) {
    for (uint32_t y = 0; y < dh; ++y) {
        int sy = std::min((int)((uint64_t)y * sh / dh), sh - 1);
        for (uint32_t x = 0; x < dw; ++x) {
            int sx = std::min((int)((uint64_t)x * sw / dw), sw - 1);
            const uint8_t* p = src + ((size_t)sy * sw + sx) * 4;
            uint8_t* q = dst + ((size_t)y * dw + x) * 4;
            q[0] = p[0]; q[1] = p[1]; q[2] = p[2]; q[3] = p[3];
        }
    }
}

static void fillFallbackLayer(uint8_t* dst, uint32_t w, uint32_t h, uint32_t mi) {
    uint32_t r = 190 + (mi * 29) % 50, g = 190 + (mi * 47) % 50, b = 190 + (mi * 67) % 50;
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = dst + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = 255;
        }
}

static void fillSolidLayer(uint8_t* dst, uint32_t w, uint32_t h, uint8_t r, uint8_t g, uint8_t b)
{
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x) {
            uint8_t* p = dst + ((size_t)y * w + x) * 4;
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
}

static void fillCheckerLayer(uint8_t* dst, uint32_t w, uint32_t h, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                             uint8_t g1, uint8_t b1, uint32_t cell)
{
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            bool alt = ((x / cell) + (y / cell)) % 2;
            uint8_t* p = dst + ((size_t)y * w + x) * 4;
            p[0] = alt ? r1 : r0;
            p[1] = alt ? g1 : g0;
            p[2] = alt ? b1 : b0;
            p[3] = 255;
        }
    }
}

static bool createMaterialTextureArray(GpuDevice device, const std::vector<uint8_t>& px, uint32_t layerCount,
                                       uint32_t layerSize, MaterialTextures& out, const char* label)
{
    rhi::SamplerDesc sd = {};
    sd.minFilter = rhi::TextureFilteringMode::Linear;
    sd.magFilter = rhi::TextureFilteringMode::Linear;
    sd.mipFilter = rhi::TextureFilteringMode::Linear;
    sd.addressU = rhi::TextureAddressingMode::Wrap;
    sd.addressV = rhi::TextureAddressingMode::Wrap;
    sd.addressW = rhi::TextureAddressingMode::ClampToEdge;
    sd.label = "material_sampler";
    rhi::ComPtr<rhi::ISampler> sampler;
    if (SLANG_FAILED(device->rhiDevice->createSampler(sd, sampler.writeRef()))) return false;

    rhi::TextureDesc rhiTd = {};
    rhiTd.type = rhi::TextureType::Texture2DArray;
    rhiTd.size = {layerSize, layerSize, 1u};
    rhiTd.arrayLength = layerCount;
    rhiTd.mipCount = 1;
    rhiTd.format = rhi::Format::RGBA8UnormSrgb;
    rhiTd.usage = rhi::TextureUsage::ShaderResource;
    rhiTd.defaultState = rhi::ResourceState::ShaderResource;
    rhiTd.sampler = sampler;
    rhiTd.label = label;

    std::vector<rhi::SubresourceData> subs(layerCount);
    size_t lbs = (size_t)layerSize * layerSize * 4;
    for (uint32_t i = 0; i < layerCount; ++i)
        subs[i] = {px.data() + (size_t)i * lbs, (size_t)(layerSize * 4), lbs};

    rhi::ComPtr<rhi::ITexture> rhiTex;
    if (SLANG_FAILED(device->rhiDevice->createTexture(rhiTd, subs.data(), rhiTex.writeRef())) || !rhiTex)
        return false;

    GpuTextureDesc td = {};
    td.type = GPU_TEXTURE_TYPE_2D;
    td.width = layerSize;
    td.height = layerSize;
    td.depth = 1;
    td.arrayLength = layerCount;
    td.mipCount = 1;
    td.format = GPU_FORMAT_RGBA8_UNORM_SRGB;
    td.usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE;
    td.label = label;
    if (gpuCreateTexture(device, &td, &out.baseColorArray) != GPU_SUCCESS) return false;

    rhi::ITexture* dummy = device->texturePool.resolve(out.baseColorArray.index, out.baseColorArray.generation);
    if (dummy) dummy->release();
    device->texturePool.slots[out.baseColorArray.index].ptr = rhiTex.detach();

    if (gpuCreateTextureView(device, out.baseColorArray, GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &out.baseColorView) !=
        GPU_SUCCESS)
        return false;

    {
        rhi::ITexture* tex = device->texturePool.resolve(out.baseColorArray.index, out.baseColorArray.generation);
        if (!tex) return false;
        rhi::TextureViewDesc viewDesc = {};
        viewDesc.sampler = sampler;
        viewDesc.label = "material_array_view";
        rhi::ComPtr<rhi::ITextureView> arrayView;
        if (SLANG_FAILED(tex->createView(viewDesc, arrayView.writeRef())) || !arrayView) return false;
        gpuDestroyTextureView(device, out.baseColorView);
        uint32_t viewIdx = device->textureViewPool.allocate(arrayView.detach());
        if (viewIdx == 0) return false;
        out.baseColorView.index = viewIdx;
        out.baseColorView.generation = device->textureViewPool.slots[viewIdx].generation;
    }

    out.layerSize = layerSize;
    out.layerCount = layerCount;
    return true;
}

bool createSimpleMaterialTextures(GpuDevice device, MaterialTextures& out)
{
    constexpr uint32_t ls = 256;
    constexpr uint32_t lc = 2;
    std::vector<uint8_t> px((size_t)lc * ls * ls * 4);

    fillCheckerLayer(px.data() + 0 * ls * ls * 4, ls, ls, 210, 210, 215, 120, 120, 128, 64);
    fillSolidLayer(px.data() + 1 * ls * ls * 4, ls, ls, 210, 95, 75);

    if (!createMaterialTextureArray(device, px, lc, ls, out, "simple_texarray")) {
        printf("Simple texture array failed\n");
        return false;
    }
    out.loadedBaseColorCount = lc;
    printf("Simple textures: %u solid/checker layers\n", lc);
    return true;
}

bool createSponzaMaterialTextures(GpuDevice device, const char* root, SponzaScene& scene, MaterialTextures& out) {
    constexpr uint32_t ls = 512;
    uint32_t lc = (uint32_t)std::max<size_t>(scene.materials.size(), 1);
    std::vector<uint8_t> px((size_t)lc * ls * ls * 4);
    uint32_t loaded = 0;

    for (uint32_t i = 0; i < lc; ++i) {
        uint8_t* layer = px.data() + (size_t)i * ls * ls * 4;
        fillFallbackLayer(layer, ls, ls, i);
        if (i >= (uint32_t)scene.materials.size() || scene.materials[i].baseColor.empty()) continue;
        char tp[kMaxPathText];
        if (!resolveTexturePath(root, scene.materials[i].baseColor, tp, sizeof(tp))) continue;
        int w = 0, h = 0, c = 0;
        stbi_uc* img = stbi_load(tp, &w, &h, &c, 4);
        if (!img || w <= 0 || h <= 0) { if (img) stbi_image_free(img); continue; }
        resizeRgbaNearest(img, w, h, layer, ls, ls);
        stbi_image_free(img);
        scene.materials[i].baseColorIndex = i;
        loaded++;
    }

    // Create sampler for the texture array
    rhi::SamplerDesc sd = {};
    sd.minFilter = rhi::TextureFilteringMode::Linear;
    sd.magFilter = rhi::TextureFilteringMode::Linear;
    sd.mipFilter = rhi::TextureFilteringMode::Linear;
    sd.addressU = rhi::TextureAddressingMode::Wrap;
    sd.addressV = rhi::TextureAddressingMode::Wrap;
    sd.addressW = rhi::TextureAddressingMode::ClampToEdge;
    sd.label = "sponza_sampler";
    rhi::ComPtr<rhi::ISampler> sampler;
    device->rhiDevice->createSampler(sd, sampler.writeRef());

    // Create texture array with initial data via RHI (C API doesn't support initial data)
    rhi::TextureDesc rhiTd = {};
    rhiTd.type = rhi::TextureType::Texture2DArray;
    rhiTd.size = {(uint32_t)ls, (uint32_t)ls, 1u};
    rhiTd.arrayLength = lc;
    rhiTd.mipCount = 1;
    rhiTd.format = rhi::Format::RGBA8UnormSrgb;
    rhiTd.usage = rhi::TextureUsage::ShaderResource;
    rhiTd.defaultState = rhi::ResourceState::ShaderResource;
    rhiTd.sampler = sampler;
    rhiTd.label = "sponza_texarray";

    std::vector<rhi::SubresourceData> subs(lc);
    size_t lbs = (size_t)ls * ls * 4;
    for (uint32_t i = 0; i < lc; ++i) {
        subs[i] = {px.data() + (size_t)i * lbs, (size_t)(ls * 4), lbs};
    }

    rhi::ComPtr<rhi::ITexture> rhiTex;
    if (SLANG_FAILED(device->rhiDevice->createTexture(rhiTd, subs.data(), rhiTex.writeRef())) || !rhiTex) {
        printf("Texture alloc failed\n");
        return false;
    }

    // Register the RHI texture in the C API handle pool
    GpuTextureDesc td = {};
    td.type = GPU_TEXTURE_TYPE_2D;
    td.width = ls; td.height = ls; td.depth = 1;
    td.arrayLength = lc; td.mipCount = 1;
    td.format = GPU_FORMAT_RGBA8_UNORM_SRGB;
    td.usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE;
    td.label = "sponza_texarray";
    if (gpuCreateTexture(device, &td, &out.baseColorArray) != GPU_SUCCESS) {
        printf("Texture handle alloc failed\n");
        return false;
    }
    // Replace the dummy RHI texture with our data-filled one
    rhi::ITexture* dummy = device->texturePool.resolve(out.baseColorArray.index, out.baseColorArray.generation);
    if (dummy) dummy->release();
    device->texturePool.slots[out.baseColorArray.index].ptr = rhiTex.detach();

    if (gpuCreateTextureView(device, out.baseColorArray, GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &out.baseColorView) !=
        GPU_SUCCESS) {
        printf("Texture array view failed\n");
        return false;
    }

    // Match 21_sponza_data_driven: sampler on the array view used for graphics sampling.
    {
        rhi::ITexture* tex = device->texturePool.resolve(out.baseColorArray.index, out.baseColorArray.generation);
        rhi::ITextureView* view =
            device->textureViewPool.resolve(out.baseColorView.index, out.baseColorView.generation);
        if (!tex || !view) {
            printf("Texture view resolve failed\n");
            return false;
        }
        rhi::TextureViewDesc viewDesc = {};
        viewDesc.sampler = sampler;
        viewDesc.label = "sponza_texarray_view";
        rhi::ComPtr<rhi::ITextureView> arrayView;
        if (SLANG_FAILED(tex->createView(viewDesc, arrayView.writeRef())) || !arrayView) {
            printf("Texture array view (sampler) failed\n");
            return false;
        }
        gpuDestroyTextureView(device, out.baseColorView);
        uint32_t viewIdx = device->textureViewPool.allocate(arrayView.detach());
        if (viewIdx == 0) {
            printf("Texture view handle alloc failed\n");
            return false;
        }
        out.baseColorView.index = viewIdx;
        out.baseColorView.generation = device->textureViewPool.slots[viewIdx].generation;
    }

    out.layerSize = ls;
    out.layerCount = lc;
    out.loadedBaseColorCount = loaded;
    printf("Textures: %u layers, %u loaded\n", lc, loaded);
    return true;
}

void destroySponzaMaterialTextures(GpuDevice device, MaterialTextures& tex) {
    if (tex.baseColorView.index) { gpuDestroyTextureView(device, tex.baseColorView); tex.baseColorView = GPU_NULL_HANDLE; }
    if (tex.baseColorArray.index) { gpuDestroyTexture(device, tex.baseColorArray); tex.baseColorArray = GPU_NULL_HANDLE; }
}
