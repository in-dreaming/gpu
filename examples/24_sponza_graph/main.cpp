// Demo 24: Sponza with render graph (gpuGraph*), CSM, point lights, SSGI, tiled cull.
// All shader resources use bindless descriptors.

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS

#include "gpu/gpu.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/rendergraph/gpu_render_graph.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <chrono>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

#include "core/camera.h"
#include "core/render_features.h"
#include "core/render_view_mode.h"
#include "core/shadow_math.h"
#include "core/sponza_loader.h"
#include "render/frame_data.h"
#include "render/frame_graph.h"
#include "render/material_textures.h"
#include "render/pass_bindings.h"
#include "render/pipelines.h"
#include "render/render_resources.h"

static constexpr uint32_t kMaxPathText = 1024;
static constexpr uint32_t kMouseButtonRight = 3;
static constexpr uint32_t kMaxPointLights = 1024;
static constexpr uint32_t kDefaultStartFrame = 1878;

static int pathJoin(char* out, size_t outSize, const char* root, const char* leaf)
{
    size_t rootLen = strlen(root), leafLen = strlen(leaf);
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
    char objPath[kMaxPathText], mtlPath[kMaxPathText];
    return pathJoin(objPath, sizeof(objPath), root, "sponza.obj") &&
           pathJoin(mtlPath, sizeof(mtlPath), root, "sponza.mtl") && fileExists(objPath) && fileExists(mtlPath);
}

static const char* resolveSponzaRoot(const char* explicitRoot, const char* argv0, char* resolved, size_t resolvedSize)
{
    if (!explicitRoot) explicitRoot = getenv("SPONZA_ROOT");
    if (explicitRoot && isSponzaRoot(explicitRoot)) return explicitRoot;

    char exeDir[kMaxPathText], candidate[kMaxPathText];
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
    for (const char* p : candidates) {
        if (isSponzaRoot(p)) {
            snprintf(resolved, resolvedSize, "%s", p);
            return resolved;
        }
    }
    return nullptr;
}

int main(int argc, char** argv)
{
    const char* requestedRoot = nullptr;
    uint32_t maxFrames = 0;
    RenderFeatures features = {};
    renderFeaturesSetBase(features);
    RenderViewMode viewMode = RenderViewMode::Final;
    const char* featuresCsv = nullptr;
    const char* viewModeStr = nullptr;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) maxFrames = (uint32_t)std::max(0, std::atoi(argv[++i]));
        else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) requestedRoot = argv[++i];
        else if (strcmp(argv[i], "--features") == 0 && i + 1 < argc) featuresCsv = argv[++i];
        else if (strcmp(argv[i], "--view-mode") == 0 && i + 1 < argc) viewModeStr = argv[++i];
        else if (strcmp(argv[i], "--help-features") == 0) {
            renderFeaturesPrintHelp();
            return 0;
        } else if (strcmp(argv[i], "--help-view-mode") == 0) {
            renderViewModePrintHelp();
            return 0;
        } else if (argv[i][0] != '-') requestedRoot = argv[i];
    }

    if (featuresCsv && !renderFeaturesParseList(features, featuresCsv)) return 1;
    if (viewModeStr && !renderViewModeParse(viewModeStr, viewMode)) {
        printf("Unknown view mode: '%s'\n", viewModeStr);
        renderViewModePrintHelp();
        return 1;
    }

    char rootBuf[kMaxPathText];
    const char* root = resolveSponzaRoot(requestedRoot, argv[0], rootBuf, sizeof(rootBuf));
    if (!root) {
        printf("Usage: 24_sponza_graph [--features <csv>] [--view-mode <name>] <path-to-Sponza>\n");
        renderFeaturesPrintHelp();
        renderViewModePrintHelp();
        return 0;
    }

    char objPath[kMaxPathText], mtlPath[kMaxPathText];
    pathJoin(objPath, sizeof(objPath), root, "sponza.obj");
    pathJoin(mtlPath, sizeof(mtlPath), root, "sponza.mtl");

    SponzaScene scene;
    if (!parseMtl(mtlPath, scene) || !parseObj(objPath, scene)) {
        printf("Parse failed\n");
        return 1;
    }
    printf("Sponza: %zu verts, %zu indices, %zu materials, %zu draws\n",
           scene.vertices.size(), scene.indices.size(), scene.materials.size(), scene.draws.size());

    gpuPlatformInit();

    GpuWindow window = nullptr;
    GpuWindowDesc wd = {"24_sponza_graph", 1280, 720, false, true, true};
    gpuCreateWindow(&wd, &window);

    GpuDevice device = nullptr;
    GpuDeviceDesc dd = {"24_sponza_graph", true, GPU_BACKEND_DEFAULT};
    if (gpuCreateDevice(&dd, &device) != GPU_SUCCESS) {
        printf("Device failed\n");
        return 1;
    }

    {
        GpuFeatureInfo bindlessInfo = {};
        if (gpuGetFeatureInfo(device, GPU_FEATURE_BINDLESS, &bindlessInfo) != GPU_SUCCESS ||
            bindlessInfo.support == GPU_FEATURE_SUPPORT_UNSUPPORTED) {
            printf("Bindless descriptors are required for this demo.\n");
            if (bindlessInfo.reason && bindlessInfo.reason[0]) printf("  reason: %s\n", bindlessInfo.reason);
            gpuDestroyDevice(device);
            gpuDestroyWindow(window);
            gpuPlatformShutdown();
            return 1;
        }
    }

    GpuSurface surface = nullptr;
    gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    GpuFormat surfaceFormat = gpuSurfaceGetPreferredFormat(surface);
    gpuSurfaceConfigure(surface, 1280, 720, surfaceFormat, true);

    MaterialTextures matTex;
    if (!createSponzaMaterialTextures(device, root, scene, matTex)) {
        printf("Textures failed\n");
        return 1;
    }

    RenderResources res;
    res.device = device;
    if (!initRenderResources(res, 1280, 720, kMaxPointLights)) {
        printf("Resources init failed\n");
        return 1;
    }
    if (!registerBindlessResources(res, matTex)) {
        printf("Bindless registration failed\n");
        return 1;
    }
    if (!validateBindlessBindings(res)) {
        printf("Bindless validation failed\n");
        return 1;
    }

    {
        GpuBufferDesc vbd = {};
        vbd.size = scene.vertices.size() * sizeof(SponzaVertex);
        vbd.usage = GPU_BUFFER_USAGE_VERTEX_BUFFER | GPU_BUFFER_USAGE_COPY_DEST;
        vbd.label = "sponza_vb";
        if (gpuCreateBufferInit(device, &vbd, scene.vertices.data(), &res.vertexBuffer) != GPU_SUCCESS) {
            printf("Vertex buffer failed\n");
            return 1;
        }
        GpuBufferDesc ibd = {};
        ibd.size = scene.indices.size() * sizeof(uint32_t);
        ibd.usage = GPU_BUFFER_USAGE_INDEX_BUFFER | GPU_BUFFER_USAGE_COPY_DEST;
        ibd.label = "sponza_ib";
        if (gpuCreateBufferInit(device, &ibd, scene.indices.data(), &res.indexBuffer) != GPU_SUCCESS) {
            printf("Index buffer failed\n");
            return 1;
        }
    }

    rhi::ComPtr<rhi::IBuffer> rhiVB, rhiIB;
    {
        auto* buf = device->bufferPool.resolve(res.vertexBuffer.index, res.vertexBuffer.generation);
        if (buf) rhiVB = buf;
        buf = device->bufferPool.resolve(res.indexBuffer.index, res.indexBuffer.generation);
        if (buf) rhiIB = buf;
    }

    DemoPipelines pipelines;
    if (!createDemoPipelines(device, pipelines, surfaceFormat)) {
        printf("Pipelines failed\n");
        return 1;
    }

    Vec3 center = {(scene.boundsMin.x + scene.boundsMax.x) * 0.5f,
                   (scene.boundsMin.y + scene.boundsMax.y) * 0.5f,
                   (scene.boundsMin.z + scene.boundsMax.z) * 0.5f};

    std::vector<PointLightData> initLights(kMaxPointLights);
    for (uint32_t i = 0; i < kMaxPointLights; i++) {
        float angle = (float)i / (float)kMaxPointLights * 2.0f * 3.14159265f;
        float radius = 150.0f + (float)(i % 6) * 100.0f;
        float height = 50.0f + (float)(i % 10) * 40.0f;
        initLights[i].position[0] = center.x + cosf(angle) * radius;
        initLights[i].position[1] = center.y + height;
        initLights[i].position[2] = center.z + sinf(angle) * radius;
        initLights[i].radius = 10.0f + (float)(i % 4) * 20.0f;
        float hue = (float)i / (float)kMaxPointLights;
        initLights[i].color[0] = 0.5f + 0.5f * sinf(hue * 6.28318f);
        initLights[i].color[1] = 0.5f + 0.5f * sinf((hue + 0.33f) * 6.28318f);
        initLights[i].color[2] = 0.5f + 0.5f * sinf((hue + 0.67f) * 6.28318f);
        initLights[i].shadowMapIndex = -1;
    }
    gpuUploadToBuffer(device, res.lightBuffer, initLights.data(), kMaxPointLights * sizeof(PointLightData), 0);

    FlyCamera cam;
    cam.position = {321.0f, 733.0f, -40.0f};
    cam.yaw = -1.62f;
    cam.pitch = 0.0f;

    FrameData fd = {};
    fd.device = device;
    fd.scene = &scene;
    fd.pipelines = &pipelines;
    fd.resources = &res;
    fd.materials = &matTex;
    fd.features = features;
    fd.viewMode = viewMode;
    fd.dirLightDir[0] = 0.55f;
    fd.dirLightDir[1] = -0.78f;
    fd.dirLightDir[2] = 0.28f;
    fd.dirLightColor[0] = 1.0f;
    fd.dirLightColor[1] = 0.94f;
    fd.dirLightColor[2] = 0.82f;
    fd.ambientColor[0] = 0.22f;
    fd.ambientColor[1] = 0.24f;
    fd.ambientColor[2] = 0.28f;
    fd.dirLightIntensity = 2.5f;
    fd.surfaceWidth = 1280;
    fd.surfaceHeight = 720;
    fd.rhiVertexBuffer = rhiVB;
    fd.rhiIndexBuffer = rhiIB;
    fd.lightCount = features.pointLights ? features.pointLightCount : 0;

    if (!bindAllPassResources(pipelines, fd, true)) {
        printf("Pass bindless binding failed\n");
        return 1;
    }
    printf("Pass bindless bindings OK\n");

    bool keys[256] = {};
    bool rmd = false;
    bool quit = false;
    uint32_t loopFrame = 0;

    auto last = std::chrono::steady_clock::now();
    auto lastPrint = last;

    printf("WASD move, Q/E up/down, right-drag look, IJKL look, [ ] cycle view mode, Esc quit.\n");
    printf("Default view: pos=(321,733,-40) yaw=-1.62 frame=%u\n", kDefaultStartFrame);
    renderFeaturesPrint(features);
    renderViewModePrint(viewMode);

    while (!quit) {
        auto now = std::chrono::steady_clock::now();
        float dt = std::chrono::duration<float>(now - last).count();
        last = now;
        if (dt <= 0 || dt > 0.1f) dt = 1.0f / 60.0f;

        GpuPlatformEvent ev;
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) quit = true;
            if (ev.type == GPU_PLATFORM_EVENT_RESIZE) {
                fd.surfaceWidth = std::max(ev.resize.width, 1u);
                fd.surfaceHeight = std::max(ev.resize.height, 1u);
                gpuSurfaceConfigure(surface, fd.surfaceWidth, fd.surfaceHeight, surfaceFormat, true);
                recreateGBuffer(res, fd.surfaceWidth, fd.surfaceHeight);
                if (!refreshGBufferBindlessHandles(res)) {
                    printf("Failed to refresh bindless G-buffer handles after resize\n");
                    quit = true;
                }
            }
            if (ev.type == GPU_PLATFORM_EVENT_KEY_DOWN || ev.type == GPU_PLATFORM_EVENT_KEY_UP) {
                uint32_t k = ev.key.keycode;
                bool dn = ev.type == GPU_PLATFORM_EVENT_KEY_DOWN;
                if (k >= 'A' && k <= 'Z') k += 'a' - 'A';
                if (k < 256) keys[k] = dn;
                if (dn && k == 27) quit = true;
                if (dn && k == ']') {
                    fd.viewMode = renderViewModeNext(fd.viewMode);
                    renderViewModePrint(fd.viewMode);
                }
                if (dn && k == '[') {
                    fd.viewMode = renderViewModePrev(fd.viewMode);
                    renderViewModePrint(fd.viewMode);
                }
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN || ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_UP) {
                if (ev.mouse.button == kMouseButtonRight) rmd = ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN;
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_MOVE && rmd) rotateCameraByMouse(cam, ev.mouse.dx, ev.mouse.dy);
        }

        updateCamera(cam, keys, dt);

        float t = (float)loopFrame * 0.02f;
        fd.cameraParams = makeCameraParams(cam, (float)fd.surfaceWidth / fd.surfaceHeight);
        fd.frameIndex = loopFrame;

        {
            float dx = fd.dirLightDir[0], dy = fd.dirLightDir[1], dz = fd.dirLightDir[2];
            float len = sqrtf(dx * dx + dy * dy + dz * dz);
            if (len > 1e-5f) {
                fd.dirLightDir[0] = dx / len;
                fd.dirLightDir[1] = dy / len;
                fd.dirLightDir[2] = dz / len;
            }
        }

        if (features.pointLights) updatePointLights(fd, center, t);
        else fd.lightCount = 0;

        if (features.dirShadows) {
            computeOrthographicCascades(
                fd.cameraParams, scene.boundsMin, scene.boundsMax, fd.dirLightDir, kCascadeCount, kShadowMapSize,
                fd.cascadeShadows);
        }

        GpuSurfaceTexture bb = nullptr;
        if (gpuSurfaceAcquireNextImage(surface, &bb) != GPU_SUCCESS) continue;

        FrameGraphContext graphCtx = {};
        graphCtx.device = device;
        graphCtx.backbuffer = bb;
        graphCtx.resources = &res;
        graphCtx.features = features;
        graphCtx.frame = &fd;

        if (!executeSponzaFrameGraph(graphCtx)) {
            gpuSurfaceTextureRelease(bb);
            continue;
        }

        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(bb);

        loopFrame++;
        if (maxFrames > 0 && loopFrame >= maxFrames) quit = true;

        if (std::chrono::duration<float>(now - lastPrint).count() > 1.0f) {
            printf("pos=(%.0f,%.0f,%.0f) yaw=%.2f frame=%u points=%u\r",
                   cam.position.x, cam.position.y, cam.position.z, cam.yaw, loopFrame, fd.lightCount);
            fflush(stdout);
            lastPrint = now;
        }
    }

    device->graphicsQueue->waitOnHost();

    destroyDemoPipelines(device, pipelines);
    destroyRenderResources(res);
    destroySponzaMaterialTextures(device, matTex);
    gpuSurfaceUnconfigure(surface);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();

    printf("\nRendered %u frames.\n", loopFrame);
    return 0;
}
