#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "gpu/gpu.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/bindless/gpu_bindless_heap.h"
#include "gpu/resource/gpu_sparse_resource.h"
#include "gpu/shader/gpu_shader_compiler.h"

#include <slang-rhi.h>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _MSC_VER
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace rhi;

static constexpr uint32_t kMaxPathText = 1024;
static constexpr uint32_t kMouseButtonRight = 3;

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

struct SponzaVertex
{
    float position[3];
    float normal[3];
    float uv[2];
    float material;
};

struct SponzaDraw
{
    uint32_t firstIndex = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
};

struct SponzaMaterial
{
    std::string name;
    std::string baseColor;
    std::string normal;
    uint32_t baseColorIndex = UINT32_MAX;
    uint32_t normalIndex = UINT32_MAX;
    uint32_t vtFirstTile = 0;
    uint32_t vtTileCount = 0;
};

struct SponzaScene
{
    std::vector<SponzaVertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<SponzaDraw> draws;
    std::vector<SponzaMaterial> materials;
    Vec3 boundsMin;
    Vec3 boundsMax;
};

struct FlyCamera
{
    Vec3 position = {0.0f, 120.0f, -450.0f};
    float yaw = 0.0f;
    float pitch = 0.0f;
};

struct CameraParams
{
    float cameraPos[3];
    float pad0;
    float right[3];
    float pad1;
    float up[3];
    float pad2;
    float forward[3];
    float pad3;
    float proj[2];
    float zParams[2];
};

struct SponzaMaterialTextures
{
    ComPtr<ISampler> sampler;
    ComPtr<ITexture> baseColorArray;
    ComPtr<ITextureView> baseColorArrayView;
    DescriptorHandle baseColorArrayHandle = {};
    DescriptorHandle samplerHandle = {};
    uint32_t layerSize = 0;
    uint32_t layerCount = 0;
    uint32_t loadedBaseColorCount = 0;
};

static const char* kSponzaShader = R"(
struct CameraParams
{
    float3 cameraPos;
    float pad0;
    float3 right;
    float pad1;
    float3 up;
    float pad2;
    float3 forward;
    float pad3;
    float2 proj;
    float2 zParams;
};

CameraParams camera;
Texture2DArray<float4> baseColorArray;
SamplerState linearSampler;

struct VSOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float material : MATERIAL;
    float viewDepth : TEXCOORD1;
};

[shader("vertex")]
VSOut vertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL)
{
    float3 d = position - camera.cameraPos;
    float vx = dot(d, camera.right);
    float vy = dot(d, camera.up);
    float vz = dot(d, camera.forward);

    VSOut output;
    output.position = float4(vx * camera.proj.x, vy * camera.proj.y, vz - camera.zParams.x, vz);
    output.normal = normalize(normal);
    output.uv = uv;
    output.material = material;
    output.viewDepth = vz;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOut input) : SV_Target
{
    float3 n = normalize(input.normal);
    float3 light = normalize(float3(0.45, 0.75, 0.35));
    float ndl = saturate(dot(n, light));
    float layer = clamp(round(input.material), 0.0, 4095.0);
    float3 albedo = baseColorArray.SampleLevel(linearSampler, float3(frac(input.uv), layer), 0).rgb;
    float fog = saturate(input.viewDepth / 2600.0);
    float3 color = albedo * (0.22 + 0.78 * ndl);
    color = lerp(color, float3(0.055, 0.065, 0.085), fog);
    return float4(color, 1.0);
}
)";

static int pathJoin(char* out, size_t outSize, const char* root, const char* leaf)
{
    size_t rootLen = strlen(root);
    size_t leafLen = strlen(leaf);
    bool needsSlash = rootLen > 0 && root[rootLen - 1] != '/' && root[rootLen - 1] != '\\';
    size_t totalLen = rootLen + (needsSlash ? 1 : 0) + leafLen;
    if (totalLen + 1 > outSize) return 0;
    memcpy(out, root, rootLen);
    size_t offset = rootLen;
    if (needsSlash) out[offset++] = '/';
    memcpy(out + offset, leaf, leafLen);
    out[totalLen] = 0;
    return 1;
}

static bool fileExists(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    return f.good();
}

static std::string textureFileToken(const std::string& value)
{
    std::istringstream ss(value);
    std::string token;
    std::string last;
    while (ss >> token) last = token;
    return last;
}

static std::string baseName(const std::string& path)
{
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

static bool resolveTexturePath(const char* root, const std::string& materialPath, char* out, size_t outSize)
{
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

static bool pathDirName(char* path)
{
    size_t len = strlen(path);
    while (len > 0) {
        char c = path[len - 1];
        if (c == '/' || c == '\\') {
            path[len - 1] = 0;
            return true;
        }
        len--;
    }
    return false;
}

static bool getExecutableDir(char* out, size_t outSize, const char* argv0)
{
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(nullptr, out, (DWORD)outSize);
    if (len > 0 && len < outSize && pathDirName(out)) return true;
#endif
    if (argv0 && argv0[0]) {
        size_t argvLen = strlen(argv0);
        if (argvLen + 1 <= outSize) {
            memcpy(out, argv0, argvLen + 1);
            return pathDirName(out);
        }
    }
    return false;
}

static bool isSponzaRoot(const char* root)
{
    char objPath[kMaxPathText];
    char mtlPath[kMaxPathText];
    return pathJoin(objPath, sizeof(objPath), root, "sponza.obj") &&
           pathJoin(mtlPath, sizeof(mtlPath), root, "sponza.mtl") &&
           fileExists(objPath) &&
           fileExists(mtlPath);
}

static const char* resolveSponzaRoot(const char* explicitRoot, const char* argv0, char* resolved, size_t resolvedSize)
{
    if (!explicitRoot) explicitRoot = getenv("SPONZA_ROOT");
    if (explicitRoot && isSponzaRoot(explicitRoot)) return explicitRoot;

    char exeDir[kMaxPathText];
    char candidate[kMaxPathText];
    if (getExecutableDir(exeDir, sizeof(exeDir), argv0)) {
        if (pathJoin(candidate, sizeof(candidate), exeDir, "Sponza") && isSponzaRoot(candidate)) {
            snprintf(resolved, resolvedSize, "%s", candidate);
            return resolved;
        }
        if (pathJoin(candidate, sizeof(candidate), exeDir, "assets/Sponza") && isSponzaRoot(candidate)) {
            snprintf(resolved, resolvedSize, "%s", candidate);
            return resolved;
        }
    }

    const char* candidates[] = {".", "Sponza", "assets/Sponza", "../Sponza", "../assets/Sponza"};
    for (const char* path : candidates) {
        if (isSponzaRoot(path)) {
            snprintf(resolved, resolvedSize, "%s", path);
            return resolved;
        }
    }
    if (explicitRoot) {
        printf("Sponza root was provided but sponza.obj/sponza.mtl were not found: %s\n", explicitRoot);
    }
    return nullptr;
}

static uint32_t addMaterial(SponzaScene& scene, const std::string& name)
{
    for (uint32_t i = 0; i < scene.materials.size(); ++i) {
        if (scene.materials[i].name == name) return i;
    }
    SponzaMaterial material;
    material.name = name;
    scene.materials.push_back(material);
    return (uint32_t)scene.materials.size() - 1;
}

static std::string trim(const std::string& s)
{
    size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

static bool parseMtl(const char* path, SponzaScene& scene)
{
    std::ifstream file(path);
    if (!file) return false;

    uint32_t current = UINT32_MAX;
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.rfind("newmtl ", 0) == 0) {
            current = addMaterial(scene, trim(line.substr(7)));
        } else if (current != UINT32_MAX && line.rfind("map_Kd ", 0) == 0) {
            scene.materials[current].baseColor = trim(line.substr(7));
        } else if (current != UINT32_MAX && line.rfind("map_bump ", 0) == 0) {
            scene.materials[current].normal = trim(line.substr(9));
        } else if (current != UINT32_MAX && line.rfind("bump ", 0) == 0) {
            scene.materials[current].normal = trim(line.substr(5));
        }
    }
    return true;
}

struct ObjKey
{
    int p;
    int t;
    int n;
    uint32_t m;
    bool operator==(const ObjKey& other) const { return p == other.p && t == other.t && n == other.n && m == other.m; }
};

struct ObjKeyHash
{
    size_t operator()(const ObjKey& key) const
    {
        uint64_t h = 1469598103934665603ull;
        auto mix = [&](uint32_t v) { h = (h ^ v) * 1099511628211ull; };
        mix((uint32_t)key.p);
        mix((uint32_t)key.t);
        mix((uint32_t)key.n);
        mix(key.m);
        return (size_t)h;
    }
};

static int resolveObjIndex(int idx, int count)
{
    if (idx > 0) return idx - 1;
    if (idx < 0) return count + idx;
    return -1;
}

static ObjKey parseFaceToken(const std::string& token, uint32_t material, int positionCount, int texcoordCount, int normalCount)
{
    ObjKey key = {-1, -1, -1, material};
    int values[3] = {0, 0, 0};
    int valueIndex = 0;
    std::string current;
    for (size_t i = 0; i <= token.size(); ++i) {
        char c = i < token.size() ? token[i] : '/';
        if (c == '/') {
            if (!current.empty() && valueIndex < 3) values[valueIndex] = std::atoi(current.c_str());
            current.clear();
            valueIndex++;
        } else {
            current.push_back(c);
        }
    }
    key.p = resolveObjIndex(values[0], positionCount);
    key.t = resolveObjIndex(values[1], texcoordCount);
    key.n = resolveObjIndex(values[2], normalCount);
    return key;
}

static Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 normalize(Vec3 v)
{
    float len = std::sqrt(std::max(dot(v, v), 1e-12f));
    return {v.x / len, v.y / len, v.z / len};
}

static bool parseObj(const char* path, SponzaScene& scene)
{
    std::ifstream file(path);
    if (!file) return false;

    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> texcoords;
    std::unordered_map<ObjKey, uint32_t, ObjKeyHash> vertexMap;

    uint32_t activeMaterial = addMaterial(scene, "default");
    SponzaDraw* activeDraw = nullptr;

    scene.boundsMin = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
    scene.boundsMax = {-std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()};

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "v") {
            Vec3 p;
            ss >> p.x >> p.y >> p.z;
            positions.push_back(p);
            scene.boundsMin.x = std::min(scene.boundsMin.x, p.x);
            scene.boundsMin.y = std::min(scene.boundsMin.y, p.y);
            scene.boundsMin.z = std::min(scene.boundsMin.z, p.z);
            scene.boundsMax.x = std::max(scene.boundsMax.x, p.x);
            scene.boundsMax.y = std::max(scene.boundsMax.y, p.y);
            scene.boundsMax.z = std::max(scene.boundsMax.z, p.z);
        } else if (tag == "vn") {
            Vec3 n;
            ss >> n.x >> n.y >> n.z;
            normals.push_back(normalize(n));
        } else if (tag == "vt") {
            Vec2 uv;
            ss >> uv.x >> uv.y;
            texcoords.push_back(uv);
        } else if (tag == "usemtl") {
            std::string name;
            ss >> name;
            activeMaterial = addMaterial(scene, name);
            activeDraw = nullptr;
        } else if (tag == "f") {
            std::vector<uint32_t> polygon;
            std::string token;
            while (ss >> token) {
                ObjKey key = parseFaceToken(token, activeMaterial, (int)positions.size(), (int)texcoords.size(), (int)normals.size());
                if (key.p < 0 || key.p >= (int)positions.size()) continue;

                auto it = vertexMap.find(key);
                uint32_t vertexIndex;
                if (it == vertexMap.end()) {
                    Vec3 p = positions[(size_t)key.p];
                    Vec3 n = key.n >= 0 && key.n < (int)normals.size() ? normals[(size_t)key.n] : Vec3{0.0f, 1.0f, 0.0f};
                    Vec2 uv = key.t >= 0 && key.t < (int)texcoords.size() ? texcoords[(size_t)key.t] : Vec2{0.0f, 0.0f};
                    SponzaVertex v = {{p.x, p.y, p.z}, {n.x, n.y, n.z}, {uv.x, uv.y}, (float)activeMaterial};
                    vertexIndex = (uint32_t)scene.vertices.size();
                    scene.vertices.push_back(v);
                    vertexMap.emplace(key, vertexIndex);
                } else {
                    vertexIndex = it->second;
                }
                polygon.push_back(vertexIndex);
            }

            if (polygon.size() >= 3) {
                if (!activeDraw) {
                    scene.draws.push_back({});
                    activeDraw = &scene.draws.back();
                    activeDraw->firstIndex = (uint32_t)scene.indices.size();
                    activeDraw->materialIndex = activeMaterial;
                }
                for (size_t i = 1; i + 1 < polygon.size(); ++i) {
                    scene.indices.push_back(polygon[0]);
                    scene.indices.push_back(polygon[i]);
                    scene.indices.push_back(polygon[i + 1]);
                    activeDraw->indexCount += 3;
                }
            }
        }
    }

    for (uint32_t i = 0; i < scene.indices.size(); i += 3) {
        SponzaVertex& a = scene.vertices[scene.indices[i + 0]];
        SponzaVertex& b = scene.vertices[scene.indices[i + 1]];
        SponzaVertex& c = scene.vertices[scene.indices[i + 2]];
        Vec3 pa = {a.position[0], a.position[1], a.position[2]};
        Vec3 pb = {b.position[0], b.position[1], b.position[2]};
        Vec3 pc = {c.position[0], c.position[1], c.position[2]};
        Vec3 face = normalize(cross(sub(pb, pa), sub(pc, pa)));
        if (std::fabs(a.normal[0]) + std::fabs(a.normal[1]) + std::fabs(a.normal[2]) < 0.01f) {
            a.normal[0] = face.x; a.normal[1] = face.y; a.normal[2] = face.z;
            b.normal[0] = face.x; b.normal[1] = face.y; b.normal[2] = face.z;
            c.normal[0] = face.x; c.normal[1] = face.y; c.normal[2] = face.z;
        }
    }

    return !scene.vertices.empty() && !scene.indices.empty();
}

static void resizeRgbaNearest(
    const uint8_t* src,
    int srcWidth,
    int srcHeight,
    uint8_t* dst,
    uint32_t dstWidth,
    uint32_t dstHeight)
{
    for (uint32_t y = 0; y < dstHeight; ++y) {
        int sy = std::min((int)((uint64_t)y * (uint32_t)srcHeight / dstHeight), srcHeight - 1);
        for (uint32_t x = 0; x < dstWidth; ++x) {
            int sx = std::min((int)((uint64_t)x * (uint32_t)srcWidth / dstWidth), srcWidth - 1);
            const uint8_t* p = src + ((size_t)sy * (size_t)srcWidth + (size_t)sx) * 4u;
            uint8_t* q = dst + ((size_t)y * (size_t)dstWidth + (size_t)x) * 4u;
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
            q[3] = p[3];
        }
    }
}

static void fillFallbackLayer(uint8_t* dst, uint32_t width, uint32_t height, uint32_t materialIndex)
{
    uint32_t r = 190u + (materialIndex * 29u) % 50u;
    uint32_t g = 190u + (materialIndex * 47u) % 50u;
    uint32_t b = 190u + (materialIndex * 67u) % 50u;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t* p = dst + ((size_t)y * width + x) * 4u;
            p[0] = (uint8_t)r;
            p[1] = (uint8_t)g;
            p[2] = (uint8_t)b;
            p[3] = 255u;
        }
    }
}

static bool createSponzaMaterialTextures(
    GpuDevice device,
    const char* root,
    SponzaScene& scene,
    SponzaMaterialTextures& out)
{
    if (!device->rhiDevice->hasFeature(Feature::Bindless)) {
        printf("Bindless descriptor handles are required for this demo on the active backend.\n");
        return false;
    }

    constexpr uint32_t layerSize = 512;
    uint32_t layerCount = (uint32_t)std::max<size_t>(scene.materials.size(), 1);
    std::vector<uint8_t> pixels((size_t)layerCount * layerSize * layerSize * 4u);

    uint32_t loaded = 0;
    for (uint32_t i = 0; i < layerCount; ++i) {
        uint8_t* layer = pixels.data() + (size_t)i * layerSize * layerSize * 4u;
        fillFallbackLayer(layer, layerSize, layerSize, i);

        if (i >= scene.materials.size() || scene.materials[i].baseColor.empty()) continue;
        char texturePath[kMaxPathText];
        if (!resolveTexturePath(root, scene.materials[i].baseColor, texturePath, sizeof(texturePath))) continue;

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* image = stbi_load(texturePath, &width, &height, &channels, 4);
        if (!image || width <= 0 || height <= 0) {
            if (image) stbi_image_free(image);
            continue;
        }
        resizeRgbaNearest(image, width, height, layer, layerSize, layerSize);
        stbi_image_free(image);
        scene.materials[i].baseColorIndex = i;
        scene.materials[i].vtFirstTile = i;
        scene.materials[i].vtTileCount = 1;
        loaded++;
    }

    SamplerDesc samplerDesc = {};
    samplerDesc.minFilter = TextureFilteringMode::Linear;
    samplerDesc.magFilter = TextureFilteringMode::Linear;
    samplerDesc.mipFilter = TextureFilteringMode::Linear;
    samplerDesc.addressU = TextureAddressingMode::Wrap;
    samplerDesc.addressV = TextureAddressingMode::Wrap;
    samplerDesc.addressW = TextureAddressingMode::ClampToEdge;
    samplerDesc.label = "sponza_linear_sampler";
    if (SLANG_FAILED(device->rhiDevice->createSampler(samplerDesc, out.sampler.writeRef())) || !out.sampler) {
        printf("Failed to create Sponza material sampler\n");
        return false;
    }

    std::vector<SubresourceData> subresources(layerCount);
    size_t layerByteSize = (size_t)layerSize * layerSize * 4u;
    for (uint32_t i = 0; i < layerCount; ++i) {
        subresources[i] = {pixels.data() + (size_t)i * layerByteSize, layerSize * 4u, layerByteSize};
    }

    TextureDesc desc = {};
    desc.type = TextureType::Texture2DArray;
    desc.size = {layerSize, layerSize, 1};
    desc.arrayLength = layerCount;
    desc.mipCount = 1;
    desc.format = Format::RGBA8UnormSrgb;
    desc.usage = TextureUsage::ShaderResource;
    desc.defaultState = ResourceState::ShaderResource;
    desc.sampler = out.sampler;
    desc.label = "sponza_base_color_vt_array";
    if (SLANG_FAILED(device->rhiDevice->createTexture(desc, subresources.data(), out.baseColorArray.writeRef())) ||
        !out.baseColorArray) {
        printf("Failed to create Sponza base-color texture array\n");
        return false;
    }
    TextureViewDesc viewDesc = {};
    viewDesc.sampler = out.sampler;
    viewDesc.label = "sponza_base_color_vt_array_view";
    if (SLANG_FAILED(out.baseColorArray->createView(viewDesc, out.baseColorArrayView.writeRef())) ||
        !out.baseColorArrayView) {
        printf("Failed to create Sponza base-color texture array view\n");
        return false;
    }
    if (SLANG_FAILED(out.baseColorArrayView->getDescriptorHandle(DescriptorHandleAccess::Read, &out.baseColorArrayHandle)) ||
        out.baseColorArrayHandle.type != DescriptorHandleType::Texture ||
        SLANG_FAILED(out.sampler->getDescriptorHandle(&out.samplerHandle)) ||
        out.samplerHandle.type != DescriptorHandleType::Sampler) {
        printf("Failed to acquire bindless descriptor handles for Sponza textures\n");
        return false;
    }

    out.layerSize = layerSize;
    out.layerCount = layerCount;
    out.loadedBaseColorCount = loaded;
    printf("Bindless VT texture array: %u material layers, %ux%u pages, %u Sponza base-color maps loaded\n",
           out.layerCount, out.layerSize, out.layerSize, out.loadedBaseColorCount);
    printf("Graphics sampling uses the same texture array through the RHI binding fallback for graphics pipelines.\n");
    return true;
}

static float clampLocal(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

static void updateCamera(FlyCamera& camera, const bool keys[256], float dt)
{
    const float moveSpeed = 430.0f;
    const float lookSpeed = 1.9f;
    if (keys['j']) camera.yaw -= lookSpeed * dt;
    if (keys['l']) camera.yaw += lookSpeed * dt;
    if (keys['i']) camera.pitch += lookSpeed * dt;
    if (keys['k']) camera.pitch -= lookSpeed * dt;
    camera.pitch = clampLocal(camera.pitch, -1.35f, 1.35f);

    float sy = std::sin(camera.yaw);
    float cy = std::cos(camera.yaw);
    Vec3 forward = {sy, 0.0f, cy};
    Vec3 right = {cy, 0.0f, -sy};
    Vec3 delta = {};
    if (keys['w']) delta = {delta.x + forward.x, delta.y, delta.z + forward.z};
    if (keys['s']) delta = {delta.x - forward.x, delta.y, delta.z - forward.z};
    if (keys['d']) delta = {delta.x + right.x, delta.y, delta.z + right.z};
    if (keys['a']) delta = {delta.x - right.x, delta.y, delta.z - right.z};
    if (keys['e']) delta.y += 1.0f;
    if (keys['q']) delta.y -= 1.0f;
    camera.position.x += delta.x * moveSpeed * dt;
    camera.position.y += delta.y * moveSpeed * dt;
    camera.position.z += delta.z * moveSpeed * dt;
}

static void rotateCameraByMouse(FlyCamera& camera, int32_t dx, int32_t dy)
{
    const float sensitivity = 0.0045f;
    camera.yaw += (float)dx * sensitivity;
    camera.pitch -= (float)dy * sensitivity;
    camera.pitch = clampLocal(camera.pitch, -1.35f, 1.35f);
}

static CameraParams makeCameraParams(const FlyCamera& camera, float aspect)
{
    float sy = std::sin(camera.yaw);
    float cy = std::cos(camera.yaw);
    float sp = std::sin(camera.pitch);
    float cp = std::cos(camera.pitch);
    Vec3 forward = normalize({sy * cp, sp, cy * cp});
    Vec3 right = normalize({cy, 0.0f, -sy});
    Vec3 up = normalize(cross(forward, right));
    float f = 1.0f / std::tan(60.0f * 0.5f * 3.1415926535f / 180.0f);

    CameraParams params = {};
    params.cameraPos[0] = camera.position.x;
    params.cameraPos[1] = camera.position.y;
    params.cameraPos[2] = camera.position.z;
    params.right[0] = right.x; params.right[1] = right.y; params.right[2] = right.z;
    params.up[0] = up.x; params.up[1] = up.y; params.up[2] = up.z;
    params.forward[0] = forward.x; params.forward[1] = forward.y; params.forward[2] = forward.z;
    params.proj[0] = f / std::max(aspect, 0.01f);
    params.proj[1] = f;
    params.zParams[0] = 3.0f;
    params.zParams[1] = 6000.0f;
    return params;
}

static bool writeShaderFile(std::string& outPath)
{
    char tempPath[kMaxPathText];
#ifdef _WIN32
    GetTempPathA((DWORD)sizeof(tempPath), tempPath);
#else
    snprintf(tempPath, sizeof(tempPath), "/tmp/");
#endif
    outPath = std::string(tempPath) + "gpu_sponza_data_driven.slang";
    std::ofstream file(outPath, std::ios::binary);
    if (!file) return false;
    file << kSponzaShader;
    return true;
}

static ComPtr<ITexture> createDepthTexture(GpuDevice device, uint32_t width, uint32_t height)
{
    TextureDesc desc = {};
    desc.type = TextureType::Texture2D;
    desc.size = {width, height, 1};
    desc.mipCount = 1;
    desc.format = Format::D32Float;
    desc.usage = TextureUsage::DepthStencil;
    desc.defaultState = ResourceState::DepthWrite;
    desc.label = "sponza_depth";
    return device->rhiDevice->createTexture(desc, nullptr);
}

int main(int argc, char** argv)
{
    const char* requestedRoot = nullptr;
    uint32_t maxFrames = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = (uint32_t)std::max(0, std::atoi(argv[++i]));
        } else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) {
            requestedRoot = argv[++i];
        } else if (argv[i][0] != '-') {
            requestedRoot = argv[i];
        }
    }

    char rootBuffer[kMaxPathText];
    const char* root = resolveSponzaRoot(requestedRoot, argv[0], rootBuffer, sizeof(rootBuffer));
    if (!root) {
        printf("Usage: 21_sponza_data_driven <path-to-jimmiebergmann-Sponza>\n");
        printf("       or --root <path>, or set SPONZA_ROOT, or configure CMake with -DGPU_SPONZA_ROOT=<path>.\n");
        printf("       Optional: --frames N renders N frames and exits for smoke tests.\n");
        printf("       Direct run searches ./Sponza and ./assets/Sponza next to the executable.\n");
        return 0;
    }

    char objPath[kMaxPathText];
    char mtlPath[kMaxPathText];
    pathJoin(objPath, sizeof(objPath), root, "sponza.obj");
    pathJoin(mtlPath, sizeof(mtlPath), root, "sponza.mtl");

    SponzaScene scene;
    if (!parseMtl(mtlPath, scene) || !parseObj(objPath, scene)) {
        printf("Failed to parse Sponza assets under %s\n", root);
        return 1;
    }
    printf("Sponza scene loaded: %zu vertices, %zu indices, %zu materials, %zu draw records\n",
           scene.vertices.size(), scene.indices.size(), scene.materials.size(), scene.draws.size());

    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); return 1; }

    GpuWindow window = nullptr;
    GpuWindowDesc winDesc = {"21_sponza_data_driven", 1280, 720, false, true, true};
    res = gpuCreateWindow(&winDesc, &window);
    if (res != GPU_SUCCESS) { printf("Window failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = nullptr;
    GpuDeviceDesc devDesc = {"21_sponza_data_driven", true};
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }

    GpuSurface surface = nullptr;
    res = gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    if (res != GPU_SUCCESS) { printf("Surface failed: %d\n", res); gpuDestroyDevice(device); gpuDestroyWindow(window); gpuPlatformShutdown(); return 1; }
    GpuFormat fmt = gpuSurfaceGetPreferredFormat(surface);
    gpuSurfaceConfigure(surface, 1280, 720, fmt, true);

    SponzaMaterialTextures materialTextures;
    if (!createSponzaMaterialTextures(device, root, scene, materialTextures)) {
        gpuSurfaceUnconfigure(surface);
        gpuDestroySurface(device, surface);
        gpuDestroyDevice(device);
        gpuDestroyWindow(window);
        gpuPlatformShutdown();
        return 1;
    }

    BufferDesc vbDesc = {};
    vbDesc.size = scene.vertices.size() * sizeof(SponzaVertex);
    vbDesc.usage = BufferUsage::VertexBuffer | BufferUsage::CopyDestination;
    vbDesc.defaultState = ResourceState::VertexBuffer;
    vbDesc.label = "sponza_vertices";
    ComPtr<IBuffer> vertexBuffer = device->rhiDevice->createBuffer(vbDesc, scene.vertices.data());

    BufferDesc ibDesc = {};
    ibDesc.size = scene.indices.size() * sizeof(uint32_t);
    ibDesc.usage = BufferUsage::IndexBuffer | BufferUsage::CopyDestination;
    ibDesc.defaultState = ResourceState::IndexBuffer;
    ibDesc.label = "sponza_indices";
    ComPtr<IBuffer> indexBuffer = device->rhiDevice->createBuffer(ibDesc, scene.indices.data());
    if (!vertexBuffer || !indexBuffer) {
        printf("Failed to create scene GPU buffers\n");
        return 1;
    }

    InputElementDesc elements[] = {
        {"POSITION", 0, Format::RGB32Float, offsetof(SponzaVertex, position), 0},
        {"NORMAL", 0, Format::RGB32Float, offsetof(SponzaVertex, normal), 0},
        {"TEXCOORD", 0, Format::RG32Float, offsetof(SponzaVertex, uv), 0},
        {"MATERIAL", 0, Format::R32Float, offsetof(SponzaVertex, material), 0},
    };
    VertexStreamDesc stream = {sizeof(SponzaVertex), InputSlotClass::PerVertex, 0};
    InputLayoutDesc layoutDesc = {};
    layoutDesc.inputElements = elements;
    layoutDesc.inputElementCount = (uint32_t)std::size(elements);
    layoutDesc.vertexStreams = &stream;
    layoutDesc.vertexStreamCount = 1;
    ComPtr<IInputLayout> inputLayout = device->rhiDevice->createInputLayout(layoutDesc);

    std::string shaderPath;
    if (!writeShaderFile(shaderPath)) {
        printf("Failed to write shader file\n");
        return 1;
    }
    GpuShaderCompiler compiler = nullptr;
    gpuCreateShaderCompiler(device, &compiler);
    GpuShaderCompileDesc compileDesc = {};
    compileDesc.sourcePath = shaderPath.c_str();
    compileDesc.entryPoint = "vertexMain";
    compileDesc.fragmentEntryPoint = "fragmentMain";
    compileDesc.target = GPU_SHADER_TARGET_SPIRV;
    GpuShaderProgram program = nullptr;
    res = gpuCompileShader(compiler, &compileDesc, &program);
    if (res != GPU_SUCCESS) {
        printf("Shader compile failed: %d %s\n", res, gpuGetShaderCompileDiagnostic(compiler));
        return 1;
    }

    ColorTargetDesc target = {};
    target.format = gpuFormatToRhi(fmt);
    RenderPipelineDesc pipelineDesc = {};
    pipelineDesc.program = program->rhiProgram;
    pipelineDesc.inputLayout = inputLayout;
    pipelineDesc.targets = &target;
    pipelineDesc.targetCount = 1;
    pipelineDesc.depthStencil.format = Format::D32Float;
    pipelineDesc.depthStencil.depthTestEnable = true;
    pipelineDesc.depthStencil.depthWriteEnable = true;
    pipelineDesc.depthStencil.depthFunc = ComparisonFunc::Less;
    pipelineDesc.rasterizer.cullMode = CullMode::None;
    pipelineDesc.rasterizer.fillMode = FillMode::Solid;
    pipelineDesc.label = "sponza_pipeline";
    ComPtr<IRenderPipeline> pipeline;
    if (SLANG_FAILED(device->rhiDevice->createRenderPipeline(pipelineDesc, pipeline.writeRef())) || !pipeline) {
        printf("Pipeline creation failed\n");
        return 1;
    }

    uint32_t surfaceWidth = 1280;
    uint32_t surfaceHeight = 720;
    ComPtr<ITexture> depthTexture = createDepthTexture(device, surfaceWidth, surfaceHeight);
    TextureViewDesc depthViewDesc = {};
    depthViewDesc.format = Format::D32Float;
    depthViewDesc.aspect = TextureAspect::DepthOnly;
    ComPtr<ITextureView> depthView = device->rhiDevice->createTextureView(depthTexture, depthViewDesc);

    FlyCamera camera;
    Vec3 center = {
        (scene.boundsMin.x + scene.boundsMax.x) * 0.5f,
        (scene.boundsMin.y + scene.boundsMax.y) * 0.5f,
        (scene.boundsMin.z + scene.boundsMax.z) * 0.5f,
    };
    camera.position = {center.x, center.y + 180.0f, scene.boundsMin.z - 450.0f};

    bool keys[256] = {};
    bool rightMouseDown = false;
    bool quit = false;
    uint32_t frame = 0;
    auto last = std::chrono::steady_clock::now();
    auto lastPrint = last;
    printf("Fly controls: WASD move, Q/E down/up, hold right mouse and drag to look, IJKL also look, Esc quit.\n");

    while (!quit) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt <= 0.0f || dt > 0.1f) dt = 1.0f / 60.0f;

        GpuPlatformEvent ev;
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) quit = true;
            if (ev.type == GPU_PLATFORM_EVENT_RESIZE) {
                surfaceWidth = std::max(ev.resize.width, 1u);
                surfaceHeight = std::max(ev.resize.height, 1u);
                gpuSurfaceConfigure(surface, surfaceWidth, surfaceHeight, fmt, true);
                depthTexture = createDepthTexture(device, surfaceWidth, surfaceHeight);
                depthView = device->rhiDevice->createTextureView(depthTexture, depthViewDesc);
            }
            if (ev.type == GPU_PLATFORM_EVENT_KEY_DOWN || ev.type == GPU_PLATFORM_EVENT_KEY_UP) {
                uint32_t key = ev.key.keycode;
                bool down = ev.type == GPU_PLATFORM_EVENT_KEY_DOWN;
                if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
                if (key < 256) keys[key] = down;
                if (down && key == 27u) quit = true;
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN || ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_UP) {
                if (ev.mouse.button == kMouseButtonRight) {
                    rightMouseDown = ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN;
                }
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_MOVE && rightMouseDown) {
                rotateCameraByMouse(camera, ev.mouse.dx, ev.mouse.dy);
            }
        }
        updateCamera(camera, keys, dt);

        GpuSurfaceTexture backbuffer = nullptr;
        if (gpuSurfaceAcquireNextImage(surface, &backbuffer) != GPU_SUCCESS) continue;

        auto queue = device->graphicsQueue;
        auto encoder = queue->createCommandEncoder();
        RenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = backbuffer->rhiTexture->getDefaultView();
        colorAttachment.loadOp = LoadOp::Clear;
        colorAttachment.storeOp = StoreOp::Store;
        colorAttachment.clearValue[0] = 0.025f;
        colorAttachment.clearValue[1] = 0.030f;
        colorAttachment.clearValue[2] = 0.040f;
        colorAttachment.clearValue[3] = 1.0f;

        RenderPassDepthStencilAttachment depthAttachment = {};
        depthAttachment.view = depthView;
        depthAttachment.depthLoadOp = LoadOp::Clear;
        depthAttachment.depthStoreOp = StoreOp::Store;
        depthAttachment.depthClearValue = 1.0f;

        RenderPassDesc passDesc = {};
        passDesc.colorAttachments = &colorAttachment;
        passDesc.colorAttachmentCount = 1;
        passDesc.depthStencilAttachment = &depthAttachment;
        auto pass = encoder->beginRenderPass(passDesc);

        auto rootObject = device->rhiDevice->createRootShaderObject(pipeline);
        CameraParams cameraParams = makeCameraParams(camera, (float)surfaceWidth / (float)surfaceHeight);
        ShaderCursor rootCursor(rootObject);
        rootCursor["camera"].setData(cameraParams);
        rootCursor["baseColorArray"].setBinding(materialTextures.baseColorArrayView);
        rootCursor["linearSampler"].setBinding(materialTextures.sampler);
        pass->bindPipeline(pipeline, rootObject);

        RenderState state = {};
        state.viewports[0] = Viewport::fromSize((float)surfaceWidth, (float)surfaceHeight);
        state.viewportCount = 1;
        state.scissorRects[0] = ScissorRect::fromSize(surfaceWidth, surfaceHeight);
        state.scissorRectCount = 1;
        state.vertexBuffers[0] = vertexBuffer;
        state.vertexBufferCount = 1;
        state.indexBuffer = indexBuffer;
        state.indexFormat = IndexFormat::Uint32;
        pass->setRenderState(state);

        for (const SponzaDraw& draw : scene.draws) {
            DrawArguments args = {};
            args.vertexCount = draw.indexCount;
            args.instanceCount = 1;
            args.startIndexLocation = draw.firstIndex;
            pass->drawIndexed(args);
        }
        pass->end();

        queue->submit(encoder->finish());
        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(backbuffer);
        frame++;
        if (maxFrames > 0 && frame >= maxFrames) quit = true;

        if (std::chrono::duration<float>(now - lastPrint).count() > 1.0f) {
            printf("Camera pos=(%.1f, %.1f, %.1f) yaw=%.2f pitch=%.2f draws=%zu frame=%u\r",
                   camera.position.x, camera.position.y, camera.position.z, camera.yaw, camera.pitch, scene.draws.size(), frame);
            fflush(stdout);
            lastPrint = now;
        }
    }

    device->graphicsQueue->waitOnHost();
    gpuDestroyShaderProgram(program);
    gpuDestroyShaderCompiler(compiler);
    gpuSurfaceUnconfigure(surface);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();
    printf("\nRendered %u frames.\n", frame);
    return 0;
}
