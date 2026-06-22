#include "material_textures.h"

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

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

    GpuBufferDesc bd = {};
    bd.size = (uint64_t)lc * ls * ls * 4;
    bd.usage = GPU_BUFFER_USAGE_COPY_DEST | GPU_BUFFER_USAGE_SHADER_RESOURCE;
    bd.label = "tex_upload";

    GpuTextureDesc td = {};
    td.type = GPU_TEXTURE_TYPE_2D;
    td.width = ls; td.height = ls; td.depth = 1;
    td.arrayLength = lc; td.mipCount = 1;
    td.format = GPU_FORMAT_RGBA8_UNORM_SRGB;
    td.usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_COPY_DEST;
    td.label = "sponza_texarray";

    if (gpuCreateTexture(device, &td, &out.baseColorArray) != GPU_SUCCESS) { printf("Texture alloc failed\n"); return false; }

    gpuCreateTextureView(device, out.baseColorArray, GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &out.baseColorView);

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
