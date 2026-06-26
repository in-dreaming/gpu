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
#include "core/dir_light_control.h"
#include "core/render_features.h"
#include "core/render_view_mode.h"
#include "core/shadow_math.h"
#include "core/sponza_loader.h"
#include "core/demo_scene.h"
#include "core/simple_scene.h"
#include "render/frame_data.h"
#include "render/frame_graph.h"
#include "render/material_textures.h"
#include "render/pass_bindings.h"
#include "render/pipelines.h"
#include "render/render_resources.h"
#include "render/shadow_diag.h"

static constexpr uint32_t kMaxPathText = 1024;
static constexpr uint32_t kMouseButtonRight = 3;
static constexpr uint32_t kMaxPointLights = 1024;
static constexpr uint32_t kDefaultStartFrame = 1878;

struct ShadowDumpRenderUser {
    GpuDevice device = nullptr;
    GpuSurface surface = nullptr;
    FrameGraphContext* graphCtx = nullptr;
};

static bool shadowDumpRenderView(void* userData, RenderViewMode mode, GpuSurfaceTexture* outSurface)
{
    auto* user = static_cast<ShadowDumpRenderUser*>(userData);
    if (!user || !user->graphCtx || !user->surface || !outSurface) return false;

    FrameData* fd = user->graphCtx->frame;
    if (!fd) return false;
    fd->viewMode = mode;

    GpuSurfaceTexture bb = nullptr;
    if (gpuSurfaceAcquireNextImage(user->surface, &bb) != GPU_SUCCESS) return false;
    user->graphCtx->backbuffer = bb;
    if (!executeSponzaFrameGraph(*user->graphCtx)) {
        gpuSurfaceTextureRelease(bb);
        return false;
    }
    user->device->graphicsQueue->waitOnHost();
    *outSurface = bb;
    return true;
}

static void shadowDumpReleaseSurface(void* userData, GpuSurfaceTexture surface)
{
    auto* user = static_cast<ShadowDumpRenderUser*>(userData);
    if (user && user->surface && surface) {
        gpuSurfacePresent(user->surface);
        gpuSurfaceTextureRelease(surface);
    }
}

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
    renderFeaturesSetSponzaDefault(features);
    RenderViewMode viewMode = RenderViewMode::Final;
    const char* featuresCsv = nullptr;
    const char* viewModeStr = nullptr;
    bool diagShadow = false;
    bool verifyShadow = false;
    bool verifyPointShadow = false;
    bool verifyLightTest = false;
    bool verifyDefault = false;
    bool verifySsgi = false;
    bool useLightTestScene = false;
    uint32_t lightTestPoints = 4;
    char dumpShadowDir[kMaxPathText] = {};
    bool dumpShadow = false;
    bool useSimpleScene = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) maxFrames = (uint32_t)std::max(0, std::atoi(argv[++i]));
        else if (strcmp(argv[i], "--simple") == 0 || strcmp(argv[i], "--simple-scene") == 0) useSimpleScene = true;
        else if (strcmp(argv[i], "--light-test") == 0) {
            useLightTestScene = true;
            useSimpleScene = true;
            if (i + 1 < argc && argv[i + 1][0] >= '0' && argv[i + 1][0] <= '9') {
                lightTestPoints = (uint32_t)std::max(1, std::min(4, atoi(argv[++i])));
            }
        } else if (strcmp(argv[i], "--verify-light-test") == 0) {
            verifyLightTest = true;
            useLightTestScene = true;
            useSimpleScene = true;
        } else if (strcmp(argv[i], "--verify-default") == 0) {
            verifyDefault = true;
            useLightTestScene = true;
            useSimpleScene = true;
            lightTestPoints = 4;
        } else if (strcmp(argv[i], "--verify-ssgi") == 0) {
            verifySsgi = true;
            useLightTestScene = true;
            useSimpleScene = true;
            lightTestPoints = 4;
        }
        else if (strcmp(argv[i], "--diag-shadow") == 0) diagShadow = true;
        else if (strcmp(argv[i], "--verify-shadow") == 0) verifyShadow = true;
        else if (strcmp(argv[i], "--verify-point-shadow") == 0) verifyPointShadow = true;
        else if (strcmp(argv[i], "--dump-shadow") == 0 && i + 1 < argc) {
            dumpShadow = true;
            snprintf(dumpShadowDir, sizeof(dumpShadowDir), "%s", argv[++i]);
        }
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

    if (diagShadow) {
        features.dirShadows = true;
        if (maxFrames == 0) maxFrames = 1;
    }
    if (verifyPointShadow) {
        useSimpleScene = true;
        features.pointShadows = true;
        features.pointLights = true;
        features.pointLightCount = 4;
        features.dirShadows = true;
        features.fog = false;
        viewMode = RenderViewMode::PointLights;
        if (maxFrames == 0) maxFrames = 12;
        diagShadow = true;
    }
    if (useSimpleScene && !verifyLightTest && !verifyPointShadow) {
        features.pointLights = true;
        features.pointShadows = true;
        features.pointLightCount = useLightTestScene ? lightTestPoints : 4u;
        features.dirLight = true;
        features.dirShadows = true;
        features.fog = false;
        if (!viewModeStr)
            viewMode = RenderViewMode::PointLights;
    }
    if (useLightTestScene && !verifyLightTest) {
        features.dirLight = true;
        features.dirShadows = true;
        features.fog = false;
        features.pointLights = true;
        features.pointShadows = true;
        features.pointLightCount = lightTestPoints;
        if (!viewModeStr)
            viewMode = RenderViewMode::Final;
    }
    if (verifySsgi) {
        features.dirLight = false;
        features.dirShadows = false;
        features.fog = false;
        features.pointLights = true;
        features.pointShadows = true;
        features.pointLightCount = 4;
        useLightTestScene = true;
        useSimpleScene = true;
        lightTestPoints = 4;
        viewMode = RenderViewMode::SSGI;
        if (maxFrames == 0)
            maxFrames = 12;
    }
    if (verifyDefault) {
        features.dirLight = true;
        features.dirShadows = true;
        features.fog = false;
        features.pointLights = true;
        features.pointShadows = true;
        features.pointLightCount = 4;
        lightTestPoints = 4;
        if (!viewModeStr)
            viewMode = RenderViewMode::Final;
        if (maxFrames == 0)
            maxFrames = 12;
    }
    if (verifyLightTest) {
        features.dirLight = false;
        features.dirShadows = false;
        features.fog = false;
        features.pointLights = true;
        features.pointLightCount = 1;
        features.pointShadows = false;
        viewMode = RenderViewMode::Final;
        maxFrames = 24;
    }
    if (verifyShadow) {
        features.dirShadows = true;
        features.fog = false;
        maxFrames = 6;
        viewMode = RenderViewMode::Shadow;
        useSimpleScene = true;
    }
    if (dumpShadow) {
        features.dirShadows = true;
        features.fog = false;
        if (maxFrames == 0) maxFrames = 1;
        diagShadow = true;
        useSimpleScene = true;
    }
    if (renderViewModeNeedsDirShadows(viewMode) && !features.dirShadows && !features.pointShadows) {
        features.dirShadows = true;
        printf("Auto-enabled dir shadows for --view-mode %s\n", renderViewModeName(viewMode));
    }
    if (renderViewModeNeedsSSGI(viewMode)) {
        // GI debug view is implemented in forward.slang (estimateIndirectGi); it does not
        // require the gbuffer/ssgi render graph passes. Auto-enabling them here crashes on
        // large scenes (Sponza) when the SSGI compute pass exhausts transient descriptors.
        if (!features.gbuffer || !features.ssgi)
            printf("Note: --view-mode gi shows forward indirect GI; use --features gbuffer,ssgi for compute SSGI.\n");
    }

    char rootBuf[kMaxPathText];
    const char* root = nullptr;
    if (!useSimpleScene) {
        root = resolveSponzaRoot(requestedRoot, argv[0], rootBuf, sizeof(rootBuf));
        if (!root) {
            printf("Usage: 24_sponza_graph [--simple] [--features <csv>] [--view-mode <name>] [path-to-Sponza]\n");
        printf("  --simple        open room + scattered props (no --light-test)\n");
            printf("  --light-test [1|2|4]  same open room + center cube cluster for point lights\n");
            printf("  --verify-light-test   auto-test 1/2 point lights +/- shadows\n");
            printf("  --verify-default      auto-test --simple --light-test 4 startup path\n");
            printf("  --verify-ssgi         auto-test gbuffer+ssgi on light-test scene\n");
            printf("  --verify-point-shadow  auto-test point-light cube shadows (simple scene)\n");
            renderFeaturesPrintHelp();
            renderViewModePrintHelp();
            return 0;
        }
    }

    DemoScene demoScene;
    if (useLightTestScene) {
        demoScene.buildLightTest();
        demoScene.lightTestCeilingCount = lightTestPoints;
    } else if (useSimpleScene) {
        demoScene.buildSimpleRoom();
    } else {
        char rootBufLoad[kMaxPathText];
        const char* rootLoad = resolveSponzaRoot(requestedRoot, argv[0], rootBufLoad, sizeof(rootBufLoad));
        if (!rootLoad || !demoScene.loadSponza(rootLoad)) {
            printf("Parse failed\n");
            return 1;
        }
    }
    demoScene.printStartupInfo();
    SponzaScene& scene = demoScene.geometry;

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
    if (demoScene.usesSimpleMaterials()) {
        if (!createSimpleMaterialTextures(device, matTex)) {
            printf("Simple textures failed\n");
            return 1;
        }
    } else if (!createSponzaMaterialTextures(device, root, scene, matTex)) {
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

    std::vector<PointLightData> initLights(kMaxPointLights);
    demoScene.fillBuiltinPointLights(initLights.data(), kMaxPointLights, 0.0f);
    gpuUploadToBuffer(device, res.lightBuffer, initLights.data(), kMaxPointLights * sizeof(PointLightData), 0);

    FlyCamera cam;
    DemoSceneLightingPreset lightingPreset = {};
    DirLightController dirLightCtrl = {};
    demoScene.setupCamera(cam);
    demoScene.setupLighting(lightingPreset);
    demoScene.setupDirLight(dirLightCtrl);
    if (verifyShadow && !demoScene.isCompactRoom()) {
        cam.position = {321.0f, 733.0f, -40.0f};
        cam.yaw = -1.62f;
        cam.pitch = 0.0f;
    }

    FrameData fd = {};
    fd.device = device;
    fd.scene = &scene;
    fd.demoScene = &demoScene;
    fd.pipelines = &pipelines;
    fd.resources = &res;
    fd.materials = &matTex;
    fd.features = features;
    fd.viewMode = viewMode;
    fd.lightTestPointCount = lightTestPoints;
    demoScene.lightTestCeilingCount = lightTestPoints;
    fd.diagShadow = diagShadow || verifyShadow || verifyPointShadow;
    memcpy(fd.dirLightDir, lightingPreset.dirLightDir, sizeof(fd.dirLightDir));
    memcpy(fd.dirLightColor, lightingPreset.dirLightColor, sizeof(fd.dirLightColor));
    memcpy(fd.ambientColor, lightingPreset.ambientColor, sizeof(fd.ambientColor));
    fd.dirLightIntensity = lightingPreset.dirLightIntensity;
    if (!demoScene.isLightTest())
        dirLightSetFromTravelDir(dirLightCtrl, fd.dirLightDir[0], fd.dirLightDir[1], fd.dirLightDir[2]);
    fd.surfaceWidth = 1280;
    fd.surfaceHeight = 720;
    fd.rhiVertexBuffer = rhiVB;
    fd.rhiIndexBuffer = rhiIB;
    fd.lightCount = features.pointLights ? features.pointLightCount : 0;

    static char diagGraphJsonPath[kMaxPathText] = {};
    if (dumpShadow && dumpShadowDir[0] != '\0') {
        snprintf(diagGraphJsonPath, sizeof(diagGraphJsonPath), "%s/shadow_diag_graph.json", dumpShadowDir);
        fd.diagGraphJsonPath = diagGraphJsonPath;
    }

    if (diagShadow) {
        printf("=== Shadow diagnostics (--diag-shadow) ===\n");
        shadowDiagVerifyShaderLayout(pipelines);
        shadowDiagPrintBindingState(fd, pipelines);
    }

    if (!bindAllPassResources(pipelines, fd, true)) {
        printf("Pass bindless binding failed\n");
        return 1;
    }
    printf("Pass bindless bindings OK\n");

    bool keys[256] = {};
    bool rmd = false;
    bool quit = false;
    int32_t pendingPlaceLightIndex = -1;
    uint32_t loopFrame = 0;
    bool verifyOk = true;
    bool verifyPointOk = true;
    bool verifyLightTestOk = true;
    bool verifyDefaultOk = true;
    bool verifySsgiOk = true;
    struct LightTestCase {
        const char* name;
        uint32_t pointCount;
        bool pointShadows;
    };
    static const LightTestCase kLightTestCases[] = {
        {"points=1 (type: point)", 1, false},
        {"points=2 (type: point)", 2, false},
        {"points=1+shadow (types: point+shadow)", 1, true},
        {"points=2+shadow (types: point+shadow)", 2, true},
    };
    static constexpr uint32_t kLightTestCaseCount = 4;
    static constexpr uint32_t kLightTestFramesPerCase = 6;
    ColorBufferStats verifyBaselineDirect = {};
    const RenderViewMode kVerifyModes[4] = {
        RenderViewMode::Shadow,
        RenderViewMode::ShadowMap,
        RenderViewMode::ShadowAtlas,
        RenderViewMode::Direct,
    };

    auto last = std::chrono::steady_clock::now();
    auto lastPrint = last;

    printf("WASD move, Q/E up/down, right-drag look, IJKL look, U/O light yaw, Y/H light pitch, [ ] view, Esc quit.\n");
    printf("  1-9 place light at camera (T toggles omni/spot), 0 clear placed lights.\n");
    if (demoScene.isCompactRoom()) {
        CameraParams look = makeCameraParams(cam, (float)fd.surfaceWidth / fd.surfaceHeight, lightingPreset.cameraFov,
                                             lightingPreset.cameraNear, lightingPreset.cameraFar);
        printf("Simple scene camera: pos=(%.1f,%.1f,%.1f) yaw=%.2f pitch=%.2f look=(%.2f,%.2f,%.2f)\n",
               cam.position.x, cam.position.y, cam.position.z, cam.yaw, cam.pitch, look.forward[0], look.forward[1],
               look.forward[2]);
    } else
        printf("Default view: pos=(321,733,-40) yaw=-1.62 frame=%u\n", kDefaultStartFrame);
    if (demoScene.isLightTest()) {
        const SimpleLightTestLayout layout = getSimpleLightTestLayout();
        printf("Light-test layout: center=(%.2f,%.2f,%.2f) height=%.2f radius=%.2f points=%u shadows=%u\n",
               layout.clusterCenter.x, layout.clusterCenter.y, layout.clusterCenter.z, layout.lightHeight,
               layout.lightRadius, lightTestPoints,
               features.pointShadows ? std::min(lightTestPoints, kMaxPointShadowSlots) : 0u);
        printf("  Keys: [ ] view mode, ,/. cycle 1/2/4 ceiling lights, T omni/spot place mode, 0 reset lights+camera, R reset camera\n");
        printf("  Tip: default view is 'final' (lit scene). Press [ ] for debug views incl. 'points'. Press 0 to reset.\n");
        printf("  Default dir-light: lightYaw=%.2f lightPitch=%.2f\n", dirLightCtrl.yaw, dirLightCtrl.pitch);
    }
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
                if (features.ssgi)
                    recreateSsgiOutput(res, fd.surfaceWidth, fd.surfaceHeight);
                if (!refreshGBufferBindlessHandles(res)) {
                    printf("Failed to refresh bindless G-buffer handles after resize\n");
                    quit = true;
                }
                if (features.ssgi && !refreshSsgiBindlessHandles(res)) {
                    printf("Failed to refresh bindless SSGI handles after resize\n");
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
                    if (renderViewModeNeedsDirShadows(fd.viewMode) && !features.dirShadows &&
                        !features.pointShadows) {
                        features.dirShadows = true;
                        fd.features = features;
                        printf("Auto-enabled dir shadows for debug view '%s'\n", renderViewModeName(fd.viewMode));
                    }
                    if (renderViewModeNeedsSSGI(fd.viewMode) && !features.gbuffer && !features.ssgi) {
                        printf("Note: view '%s' uses forward indirect GI; --features gbuffer,ssgi enables compute pass.\n",
                               renderViewModeName(fd.viewMode));
                    }
                    renderViewModePrint(fd.viewMode);
                }
                if (dn && k == '[') {
                    fd.viewMode = renderViewModePrev(fd.viewMode);
                    if (renderViewModeNeedsDirShadows(fd.viewMode) && !features.dirShadows &&
                        !features.pointShadows) {
                        features.dirShadows = true;
                        fd.features = features;
                        printf("Auto-enabled dir shadows for debug view '%s'\n", renderViewModeName(fd.viewMode));
                    }
                    if (renderViewModeNeedsSSGI(fd.viewMode) && !features.gbuffer && !features.ssgi) {
                        printf("Note: view '%s' uses forward indirect GI; --features gbuffer,ssgi enables compute pass.\n",
                               renderViewModeName(fd.viewMode));
                    }
                    renderViewModePrint(fd.viewMode);
                }
                if (dn && k == '0' && !verifyLightTest) {
                    fd.placedPointLights.clearAll();
                    if (demoScene.isLightTest()) {
                        features.pointLightCount = lightTestPoints;
                        fd.features = features;
                        demoScene.setupCamera(cam);
                        demoScene.setupDirLight(dirLightCtrl);
                        printf("Cleared placed lights; restored %u ceiling light(s) and reset camera.\n",
                               lightTestPoints);
                    } else {
                        printf("Cleared all camera-placed point lights.\n");
                    }
                }
                if (dn && (k == 'r' || k == 'R') && demoScene.isLightTest() && !verifyLightTest) {
                    demoScene.setupCamera(cam);
                    demoScene.setupDirLight(dirLightCtrl);
                    printf("Reset camera to light-test default.\n");
                }
                if (dn && !verifyLightTest && k >= '1' && k <= '9') {
                    pendingPlaceLightIndex = (int32_t)(k - '1');
                }
                if (dn && !verifyLightTest && (k == 't' || k == 'T')) {
                    fd.placedPointLights.placeKind = (fd.placedPointLights.placeKind == PlacedLightKind::Spot)
                                                         ? PlacedLightKind::Omni
                                                         : PlacedLightKind::Spot;
                    printf("Place mode: %s point light\n",
                           fd.placedPointLights.placeKind == PlacedLightKind::Omni ? "omni" : "spot");
                }
                if (dn && demoScene.isLightTest() && !verifyLightTest && (k == ',' || k == '.')) {
                    static const uint32_t kLightTestCounts[] = {1u, 2u, 4u};
                    uint32_t idx = 0;
                    for (uint32_t i = 0; i < 3; i++) {
                        if (lightTestPoints == kLightTestCounts[i]) {
                            idx = i;
                            break;
                        }
                    }
                    if (k == ',')
                        idx = (idx + 2u) % 3u;
                    else
                        idx = (idx + 1u) % 3u;
                    lightTestPoints = kLightTestCounts[idx];
                    demoScene.lightTestCeilingCount = lightTestPoints;
                    fd.lightTestPointCount = lightTestPoints;
                    features.pointLightCount = lightTestPoints;
                    fd.features = features;
                    printf("Light-test: %u point light(s)\n", lightTestPoints);
                }
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN || ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_UP) {
                if (ev.mouse.button == kMouseButtonRight) rmd = ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN;
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_MOVE && rmd) rotateCameraByMouse(cam, ev.mouse.dx, ev.mouse.dy);
        }

        updateCamera(cam, keys, dt, lightingPreset.cameraMoveSpeed, lightingPreset.cameraLookSpeed);
        updateDirLightController(dirLightCtrl, keys, dt);
        dirLightWriteTravelDir(dirLightCtrl, fd.dirLightDir);

        float t = (float)loopFrame * 0.02f;

        if (verifyLightTest) {
            const uint32_t caseIndex = loopFrame / kLightTestFramesPerCase;
            if (caseIndex >= kLightTestCaseCount) {
                quit = true;
                continue;
            }
            const LightTestCase& tc = kLightTestCases[caseIndex];
            const uint32_t frameInCase = loopFrame % kLightTestFramesPerCase;
            features.pointLights = true;
            features.pointLightCount = tc.pointCount;
            features.pointShadows = tc.pointShadows;
            features.dirLight = false;
            features.dirShadows = false;
            features.fog = false;
            fd.lightTestPointCount = tc.pointCount;
            demoScene.lightTestCeilingCount = tc.pointCount;
            fd.features = features;
            if (frameInCase == 0) {
                if (tc.pointShadows && tc.pointCount == 1)
                    fd.viewMode = RenderViewMode::PointLights;
                else
                    fd.viewMode = RenderViewMode::Final;
                printf("[verify-light] case %u/%u: %s\n", caseIndex + 1, kLightTestCaseCount, tc.name);
            }
        }

        fd.cameraParams = makeCameraParams(cam, (float)fd.surfaceWidth / fd.surfaceHeight, lightingPreset.cameraFov,
                                           lightingPreset.cameraNear, lightingPreset.cameraFar);

        if (pendingPlaceLightIndex >= 0) {
            const uint32_t lightIndex = (uint32_t)pendingPlaceLightIndex;
            if (lightIndex < PlacedPointLights::kMaxSlots) {
                demoScene.placeLightAtCamera(fd.placedPointLights, lightIndex, fd.cameraParams,
                                             fd.placedPointLights.placeKind);
                features.pointLights = true;
                features.pointShadows = true;
                features.pointLightCount =
                    demoScene.resolvePointLightUploadCount(features.pointLightCount, fd.placedPointLights);
                fd.features = features;
                if (demoScene.isLightTest()) {
                    const uint32_t lightBase = demoScene.placedLightBufferOffset();
                    printf("  (light-test: ceiling lights 1-%u kept; key %u is extra light %u)\n", lightTestPoints,
                           lightIndex + 1, lightBase + lightIndex + 1);
                }
            }
            pendingPlaceLightIndex = -1;
        }

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

        if (features.pointLights) updatePointLights(fd, scene.boundsMin, scene.boundsMax, t);
        else fd.lightCount = 0;

        if (verifyShadow) {
            if (loopFrame == 0) {
                features.dirShadows = false;
                fd.features = features;
                fd.viewMode = RenderViewMode::Direct;
            } else {
                features.dirShadows = true;
                fd.features = features;
                if (loopFrame == 5)
                    fd.viewMode = RenderViewMode::Final;
                else if (loopFrame >= 1 && loopFrame <= 4)
                    fd.viewMode = kVerifyModes[loopFrame - 1];
            }
        }

        if (features.dirShadows) {
            computeOrthographicCascades(
                fd.cameraParams, scene.boundsMin, scene.boundsMax, fd.dirLightDir, kCascadeCount, kShadowMapSize,
                fd.cascadeShadows);
            if (diagShadow) {
                shadowDiagPrintCpuClip(fd.cascadeShadows[0], "scene_center",
                    (scene.boundsMin.x + scene.boundsMax.x) * 0.5f,
                    (scene.boundsMin.y + scene.boundsMax.y) * 0.5f,
                    (scene.boundsMin.z + scene.boundsMax.z) * 0.5f);
                shadowDiagPrintCpuClip(fd.cascadeShadows[0], "camera",
                    cam.position.x, cam.position.y, cam.position.z);
            }
        }

        GpuSurfaceTexture bb = nullptr;
        bool skipPresent = false;

        if (dumpShadow && loopFrame == 0) {
            ShadowDumpRenderUser dumpUser = {};
            dumpUser.device = device;
            dumpUser.surface = surface;

            FrameGraphContext graphCtx = {};
            graphCtx.device = device;
            graphCtx.resources = &res;
            graphCtx.features = features;
            graphCtx.frame = &fd;
            dumpUser.graphCtx = &graphCtx;

            ShadowDumpContext dumpCtx = {};
            dumpCtx.device = device;
            dumpCtx.surfaceWidth = fd.surfaceWidth;
            dumpCtx.surfaceHeight = fd.surfaceHeight;
            dumpCtx.frame = &fd;
            dumpCtx.resources = &res;
            dumpCtx.outputDir = dumpShadowDir;
            dumpCtx.boundsMin = &scene.boundsMin;
            dumpCtx.boundsMax = &scene.boundsMax;
            dumpCtx.camera = &cam;
            dumpCtx.renderView = shadowDumpRenderView;
            dumpCtx.releaseSurface = shadowDumpReleaseSurface;
            dumpCtx.renderUserData = &dumpUser;

            printf("=== Shadow dump to %s ===\n", dumpShadowDir);
            if (!shadowDiagDumpSession(dumpCtx)) {
                printf("=== Shadow dump: FAIL ===\n");
                quit = true;
            } else {
                printf("=== Shadow dump: OK ===\n");
            }
            skipPresent = true;
        } else {
            if (gpuSurfaceAcquireNextImage(surface, &bb) != GPU_SUCCESS) continue;

            fd.features = features;

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
        }

        if (verifyLightTest && (loopFrame % kLightTestFramesPerCase) == (kLightTestFramesPerCase - 1)) {
            device->graphicsQueue->waitOnHost();
            const uint32_t caseIndex = loopFrame / kLightTestFramesPerCase;
            const LightTestCase& tc = kLightTestCases[caseIndex];
            ColorBufferStats colorStats = {};
            if (shadowDiagReadbackSurface(device, bb, fd.surfaceWidth, fd.surfaceHeight, colorStats)) {
                shadowDiagPrintColorStats(colorStats, tc.name);
                char failMsg[256] = {};
                bool pass = false;
                if (tc.pointShadows && tc.pointCount == 1)
                    pass = shadowDiagCheckPointLightsView(colorStats, failMsg, sizeof(failMsg));
                else if (tc.pointShadows)
                    pass = shadowDiagCheckLightTestFinal(colorStats, tc.pointCount, tc.pointShadows, failMsg,
                                                         sizeof(failMsg));
                else
                    pass = shadowDiagCheckLightTestFinal(colorStats, tc.pointCount, false, failMsg, sizeof(failMsg));
                if (!pass) {
                    printf("[verify-light] FAIL %s: %s\n", tc.name, failMsg);
                    verifyLightTestOk = false;
                } else {
                    printf("[verify-light] PASS %s\n", tc.name);
                }
            } else {
                printf("[verify-light] FAIL %s: readback failed\n", tc.name);
                verifyLightTestOk = false;
            }
        }

        if (verifySsgi && loopFrame == 9) {
            device->graphicsQueue->waitOnHost();
            ColorBufferStats colorStats = {};
            if (shadowDiagReadbackSurface(device, bb, fd.surfaceWidth, fd.surfaceHeight, colorStats)) {
                shadowDiagPrintColorStats(colorStats, "ssgi_gi");
                char failMsg[256] = {};
                if (!shadowDiagCheckSsgiView(colorStats, failMsg, sizeof(failMsg))) {
                    printf("[verify-ssgi] FAIL ssgi_gi: %s\n", failMsg);
                    verifySsgiOk = false;
                } else {
                    printf("[verify-ssgi] PASS ssgi_gi\n");
                }
            } else {
                printf("[verify-ssgi] FAIL ssgi_gi: readback failed\n");
                verifySsgiOk = false;
            }
        }

        if (verifyDefault) {
            if (loopFrame == 5)
                fd.viewMode = RenderViewMode::PointLights;
            else if (loopFrame == 8)
                fd.viewMode = RenderViewMode::Shadow;
            else if (loopFrame == 10)
                fd.viewMode = RenderViewMode::Final;
        }

        if (verifyDefault && (loopFrame == 7 || loopFrame == 9 || loopFrame == 11)) {
            device->graphicsQueue->waitOnHost();
            ColorBufferStats colorStats = {};
            const char* label =
                (loopFrame == 7) ? "default_points" : (loopFrame == 9) ? "default_shadow" : "default_final";
            if (shadowDiagReadbackSurface(device, bb, fd.surfaceWidth, fd.surfaceHeight, colorStats)) {
                shadowDiagPrintColorStats(colorStats, label);
                char failMsg[256] = {};
                bool pass = false;
                if (loopFrame == 7)
                    pass = shadowDiagCheckDefaultLightTestPoints(colorStats, failMsg, sizeof(failMsg));
                else if (loopFrame == 9)
                    pass = shadowDiagCheckDefaultLightTestShadow(colorStats, failMsg, sizeof(failMsg));
                else
                    pass = shadowDiagCheckDefaultLightTestFinal(colorStats, failMsg, sizeof(failMsg));
                if (!pass) {
                    printf("[verify-default] FAIL %s: %s\n", label, failMsg);
                    verifyDefaultOk = false;
                } else {
                    printf("[verify-default] PASS %s\n", label);
                }
            } else {
                printf("[verify-default] FAIL %s: readback failed\n", label);
                verifyDefaultOk = false;
            }
        }

        if (verifyPointShadow && loopFrame == 8) {
            device->graphicsQueue->waitOnHost();
            ColorBufferStats colorStats = {};
            if (shadowDiagReadbackSurface(device, bb, fd.surfaceWidth, fd.surfaceHeight, colorStats)) {
                shadowDiagPrintColorStats(colorStats, "point_lights");
                char failMsg[256] = {};
                if (!shadowDiagCheckPointLightsView(colorStats, failMsg, sizeof(failMsg))) {
                    printf("[verify-point] FAIL points view: %s\n", failMsg);
                    verifyPointOk = false;
                } else {
                    printf("[verify-point] PASS points view (shadow darkening visible)\n");
                }
            } else {
                printf("[verify-point] FAIL points view: color readback failed\n");
                verifyPointOk = false;
            }
        }

        if (verifyShadow && loopFrame < 6) {
            device->graphicsQueue->waitOnHost();
            ColorBufferStats colorStats = {};
            const char* modeName = (loopFrame == 0) ? "baseline_direct" : renderViewModeName(fd.viewMode);
            if (shadowDiagReadbackSurface(device, bb, fd.surfaceWidth, fd.surfaceHeight, colorStats)) {
                shadowDiagPrintColorStats(colorStats, modeName);
                if (loopFrame == 0) {
                    verifyBaselineDirect = colorStats;
                } else if (loopFrame == 4) {
                    float delta = verifyBaselineDirect.meanLuma - colorStats.meanLuma;
                    float minDelta = demoScene.isCompactRoom() ? 0.002f : 0.005f;
                    printf("[verify] direct A/B mean luma delta (no-shadow - shadow) = %.4f\n", delta);
                    if (delta < minDelta) {
                        printf("[verify] FAIL direct: insufficient shadow darkening (delta=%.4f, need>=%.4f)\n", delta,
                               minDelta);
                        verifyOk = false;
                    } else {
                        printf("[verify] PASS direct A/B darkening\n");
                    }
                }
                if (loopFrame > 0) {
                    char failMsg[256] = {};
                    auto checkView = demoScene.isCompactRoom() ? shadowDiagCheckViewModeSimple : shadowDiagCheckViewMode;
                    if (!checkView(colorStats, fd.viewMode, failMsg, sizeof(failMsg))) {
                        if (fd.viewMode != RenderViewMode::Direct) {
                            printf("[verify] FAIL %s: %s\n", modeName, failMsg);
                            verifyOk = false;
                        }
                    } else if (fd.viewMode != RenderViewMode::Direct) {
                        printf("[verify] PASS %s\n", modeName);
                    }
                }
            } else {
                printf("[verify] FAIL %s: color readback failed\n", modeName);
                verifyOk = false;
            }
        }

        if (!skipPresent) {
            gpuSurfacePresent(surface);
            gpuSurfaceTextureRelease(bb);
        }

        loopFrame++;
        if (maxFrames > 0 && loopFrame >= maxFrames) quit = true;

        if (std::chrono::duration<float>(now - lastPrint).count() > 1.0f) {
            printf("pos=(%.0f,%.0f,%.0f) camYaw=%.2f lightYaw=%.2f lightPitch=%.2f frame=%u\r",
                   cam.position.x, cam.position.y, cam.position.z, cam.yaw, dirLightCtrl.yaw, dirLightCtrl.pitch,
                   loopFrame);
            fflush(stdout);
            lastPrint = now;
        }
    }

    device->graphicsQueue->waitOnHost();

    if (diagShadow) {
        shadowDiagPrintFrameCounters(fd);
        {
            ShadowDepthStats sceneStats = {};
            if (shadowDiagReadbackCascadeDepth(device, res.sceneDepth, sceneStats))
                shadowDiagPrintStats(sceneStats, -1);
        }
        for (int ci = 0; ci < 4; ci++) {
            ShadowDepthStats stats = {};
            if (shadowDiagReadbackCascadeDepth(device, res.cascadeDepth[ci], stats))
                shadowDiagPrintStats(stats, ci);
        }
        printf("=== End shadow diagnostics ===\n");
    }

    if (verifySsgi) {
        printf("=== SSGI verify: %s ===\n", verifySsgiOk ? "PASS" : "FAIL");
        if (!verifySsgiOk) return 1;
    }

    if (verifyDefault) {
        printf("=== Default light-test verify: %s ===\n", verifyDefaultOk ? "PASS" : "FAIL");
        if (!verifyDefaultOk) return 1;
    }

    if (verifyLightTest) {
        printf("=== Light-test verify: %s ===\n", verifyLightTestOk ? "PASS" : "FAIL");
        if (!verifyLightTestOk) return 1;
    }

    if (verifyPointShadow) {
        printf("=== Point shadow verify: %s ===\n", verifyPointOk ? "PASS" : "FAIL");
        if (!verifyPointOk) return 1;
    }

    if (verifyShadow) {
        printf("=== Shadow verify: %s ===\n", verifyOk ? "PASS" : "FAIL");
        if (!verifyOk) return 1;
    }

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
