// Demo 24: Sponza with Render Graph, Cascaded Shadow Maps, 1K+ Lights, SSGI
//
// Features:
// - Render graph based frame orchestration (no manual barriers)
// - Cascaded shadow maps (CSM) with 4 cascades at 2048x2048 each
// - 1024 animated point lights with per-frame GPU upload
// - Screen-space global illumination (SSGI) compute pass at half-res
// - Tile-based light culling via compute shader
// - Per-frame graph compile/execute with automatic resource state transitions
//
// Controls: WASD move, Q/E down/up, right-mouse drag look, IJKL look, Esc quit

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
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <algorithm>
#include <limits>

#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>

using namespace rhi;

// Internal modules
#include "core/types.h"
#include "core/camera.h"
#include "core/sponza_loader.h"
#include "render/render_resources.h"
#include "render/material_textures.h"
#include "render/pipelines.h"

static constexpr uint32_t kMaxPathText = 1024;
static constexpr uint32_t kMouseButtonRight = 3;
static constexpr uint32_t kShadowMapSize = 2048;
static constexpr uint32_t kMaxPointLights = 1024;

// ============================================================================
// Path utilities (shared with sponza_loader)
// ============================================================================

static int pathJoin(char* out, size_t outSize, const char* root, const char* leaf) {
    size_t rootLen = strlen(root), leafLen = strlen(leaf);
    bool needsSlash = rootLen > 0 && root[rootLen-1] != '/' && root[rootLen-1] != '\\';
    size_t totalLen = rootLen + (needsSlash?1:0) + leafLen;
    if (totalLen+1 > outSize) return 0;
    memcpy(out, root, rootLen); size_t offset = rootLen;
    if (needsSlash) out[offset++] = '/';
    memcpy(out+offset, leaf, leafLen);
    out[totalLen] = 0; return 1;
}

static bool fileExists(const char* path) { std::ifstream f(path, std::ios::binary); return f.good(); }

static bool pathDirName(char* path) {
    size_t len = strlen(path);
    while (len > 0) { char c = path[len-1]; if (c=='/'||c=='\\') { path[len-1]=0; return true; } len--; }
    return false;
}

static bool getExecutableDir(char* out, size_t outSize, const char* argv0) {
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(nullptr, out, (DWORD)outSize);
    if (len > 0 && len < outSize && pathDirName(out)) return true;
#endif
    if (argv0 && argv0[0]) { size_t argvLen = strlen(argv0); if (argvLen+1 <= outSize) { memcpy(out, argv0, argvLen+1); return pathDirName(out); } }
    return false;
}

static bool isSponzaRoot(const char* root) {
    char objPath[kMaxPathText], mtlPath[kMaxPathText];
    return pathJoin(objPath, sizeof(objPath), root, "sponza.obj") &&
           pathJoin(mtlPath, sizeof(mtlPath), root, "sponza.mtl") &&
           fileExists(objPath) && fileExists(mtlPath);
}

static const char* resolveSponzaRoot(const char* explicitRoot, const char* argv0, char* resolved, size_t resolvedSize) {
    if (!explicitRoot) explicitRoot = getenv("SPONZA_ROOT");
    if (explicitRoot && isSponzaRoot(explicitRoot)) return explicitRoot;
    char exeDir[kMaxPathText], candidate[kMaxPathText];
    if (getExecutableDir(exeDir, sizeof(exeDir), argv0)) {
        if (pathJoin(candidate, sizeof(candidate), exeDir, "Sponza") && isSponzaRoot(candidate)) { snprintf(resolved, resolvedSize, "%s", candidate); return resolved; }
        if (pathJoin(candidate, sizeof(candidate), exeDir, "assets/Sponza") && isSponzaRoot(candidate)) { snprintf(resolved, resolvedSize, "%s", candidate); return resolved; }
    }
    const char* candidates[] = {".", "Sponza", "assets/Sponza", "../Sponza", "../assets/Sponza"};
    for (const char* p : candidates) { if (isSponzaRoot(p)) { snprintf(resolved, resolvedSize, "%s", p); return resolved; } }
    return nullptr;
}

// ============================================================================
// Per-frame data shared between render graph callbacks
// ============================================================================

// Matrix utility: build a 4x4 view matrix from camera basis
static void buildViewMatrix(const CameraParams& cam, float* out) {
    memset(out, 0, sizeof(float) * 16);
    // Rotation: right, up, -forward as rows
    out[0] = cam.right[0]; out[1] = cam.right[1]; out[2] = cam.right[2];
    out[4] = cam.up[0];    out[5] = cam.up[1];    out[6] = cam.up[2];
    out[8] = -cam.forward[0]; out[9] = -cam.forward[1]; out[10] = -cam.forward[2];
    // Translation: -dot(right, pos), -dot(up, pos), dot(forward, pos)
    float tx = cam.right[0]*cam.cameraPos[0] + cam.right[1]*cam.cameraPos[1] + cam.right[2]*cam.cameraPos[2];
    float ty = cam.up[0]*cam.cameraPos[0] + cam.up[1]*cam.cameraPos[1] + cam.up[2]*cam.cameraPos[2];
    float tz = cam.forward[0]*cam.cameraPos[0] + cam.forward[1]*cam.cameraPos[1] + cam.forward[2]*cam.cameraPos[2];
    out[3] = -tx; out[7] = -ty; out[11] = tz;
    out[15] = 1.0f;
}

// Build a 4x4 projection matrix from CameraParams (custom reversed-z style)
static void buildProjMatrix(const CameraParams& cam, float* out) {
    memset(out, 0, sizeof(float) * 16);
    float fx = cam.proj[0], fy = cam.proj[1];
    float n = cam.zParams[0], f = cam.zParams[1];
    out[0] = fx;  out[5] = fy;  out[10] = f / (f - n);  out[11] = 1.0f;
    out[14] = (-f * n) / (f - n);
    // W = vz; Z = vz * f/(f-n) - f*n/(f-n); homogenous division gives: Z_clip = (f/(f-n)) - (f*n/(f-n))/vz
}

struct FrameData {
    GpuDevice device;
    SponzaScene* scene;
    DemoPipelines* pipelines;
    RenderResources* resources;
    MaterialTextures* materials;
    GpuSamplerHandle linearSamplerHandle;
    GpuSamplerHandle shadowSamplerHandle;

    // Per-frame camera
    CameraParams cameraParams;
    CameraParams shadowCameras[4];

    // Per-frame light data
    uint32_t lightCount;
    float dirLightDir[3];
    float dirLightColor[3];
    float dirLightIntensity;
    float ambientColor[3];

    // Per-frame viewport
    uint32_t surfaceWidth, surfaceHeight;
    uint32_t frameIndex;

    // RHI-level geometry buffers for callback access
    ComPtr<IBuffer> rhiVertexBuffer;
    ComPtr<IBuffer> rhiIndexBuffer;
};

// Forward declarations of callbacks
static void shadowPassCallback(GpuGraphPassContext* ctx, void* userData);
static void forwardPassCallback(GpuGraphPassContext* ctx, void* userData);
static void ssgiPassCallback(GpuGraphPassContext* ctx, void* userData);
static void lightCullPassCallback(GpuGraphPassContext* ctx, void* userData);

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
    const char* requestedRoot = nullptr;
    uint32_t maxFrames = 0;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) maxFrames = (uint32_t)std::max(0, std::atoi(argv[++i]));
        else if (strcmp(argv[i], "--root") == 0 && i + 1 < argc) requestedRoot = argv[++i];
        else if (argv[i][0] != '-') requestedRoot = argv[i];
    }

    char rootBuf[kMaxPathText];
    const char* root = resolveSponzaRoot(requestedRoot, argv[0], rootBuf, sizeof(rootBuf));
    if (!root) { printf("Usage: 24_sponza_graph <path-to-Sponza>\n"); return 0; }

    char objPath[kMaxPathText], mtlPath[kMaxPathText];
    pathJoin(objPath, sizeof(objPath), root, "sponza.obj");
    pathJoin(mtlPath, sizeof(mtlPath), root, "sponza.mtl");

    SponzaScene scene;
    if (!parseMtl(mtlPath, scene) || !parseObj(objPath, scene)) {
        printf("Parse failed\n"); return 1;
    }
    printf("Sponza: %zu verts, %zu indices, %zu materials, %zu draws\n",
           scene.vertices.size(), scene.indices.size(), scene.materials.size(), scene.draws.size());

    // Init platform
    gpuPlatformInit();

    GpuWindow window = nullptr;
    GpuWindowDesc wd = {"24_sponza_graph", 1280, 720, false, true, true};
    gpuCreateWindow(&wd, &window);

    GpuDevice device = nullptr;
    GpuDeviceDesc dd = {"24_sponza_graph", true, GPU_BACKEND_DEFAULT};
    if (gpuCreateDevice(&dd, &device) != GPU_SUCCESS) { printf("Device failed\n"); return 1; }

    GpuSurface surface = nullptr;
    gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    GpuFormat surfaceFormat = gpuSurfaceGetPreferredFormat(surface);
    gpuSurfaceConfigure(surface, 1280, 720, surfaceFormat, true);

    // Material textures
    MaterialTextures matTex;
    if (!createSponzaMaterialTextures(device, root, scene, matTex)) { printf("Textures failed\n"); return 1; }

    // Render resources (depth, SSGI, cascades, buffers)
    RenderResources res;
    res.device = device;
    if (!initRenderResources(res, 1280, 720, kMaxPointLights)) { printf("Resources init failed\n"); return 1; }

    // Create geometry buffers
    {
        GpuBufferDesc vbd = {};
        vbd.size = scene.vertices.size() * sizeof(SponzaVertex);
        vbd.usage = GPU_BUFFER_USAGE_VERTEX_BUFFER | GPU_BUFFER_USAGE_COPY_DEST;
        vbd.label = "sponza_vb";
        if (gpuCreateBufferInit(device, &vbd, scene.vertices.data(), &res.vertexBuffer) != GPU_SUCCESS) {
            printf("Vertex buffer failed\n"); return 1;
        }
        GpuBufferDesc ibd = {};
        ibd.size = scene.indices.size() * sizeof(uint32_t);
        ibd.usage = GPU_BUFFER_USAGE_INDEX_BUFFER | GPU_BUFFER_USAGE_COPY_DEST;
        ibd.label = "sponza_ib";
        if (gpuCreateBufferInit(device, &ibd, scene.indices.data(), &res.indexBuffer) != GPU_SUCCESS) {
            printf("Index buffer failed\n"); return 1;
        }
    }

    // Get RHI-level buffers for callback access
    ComPtr<IBuffer> rhiVB, rhiIB;
    {
        auto* buf = device->bufferPool.resolve(res.vertexBuffer.index, res.vertexBuffer.generation);
        if (buf) rhiVB = buf;
        buf = device->bufferPool.resolve(res.indexBuffer.index, res.indexBuffer.generation);
        if (buf) rhiIB = buf;
    }

    // Pipelines
    DemoPipelines pipelines;
    if (!createDemoPipelines(device, pipelines, surfaceFormat)) { printf("Pipelines failed\n"); return 1; }

    // Sampler handles for the C API (needed for graph import/texture view binding)
    GpuSamplerHandle linearSamplerHandle = res.linearSampler;

    // Light data: generate 1024 lights around Sponza
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
        initLights[i].radius = 80.0f + (float)(i % 4) * 60.0f;
        float hue = (float)i / (float)kMaxPointLights;
        initLights[i].color[0] = 0.5f + 0.5f * sinf(hue * 6.28318f);
        initLights[i].color[1] = 0.5f + 0.5f * sinf((hue + 0.33f) * 6.28318f);
        initLights[i].color[2] = 0.5f + 0.5f * sinf((hue + 0.67f) * 6.28318f);
    }
    gpuUploadToBuffer(device, res.lightBuffer, initLights.data(), kMaxPointLights * sizeof(PointLightData), 0);

    // Light uniforms
    float dirLightDir[3] = {0.45f, -0.75f, -0.35f};
    float dirLightColor[3] = {1.0f, 0.95f, 0.85f};
    float ambientColor[3] = {0.18f, 0.20f, 0.25f};
    float dirLightIntensity = 2.5f;

    // Camera setup
    FlyCamera cam;
    cam.position = {center.x, center.y + 180, scene.boundsMin.z - 450};

    // ---- Frame data for callbacks ----
    FrameData fd;
    fd.device = device;
    fd.scene = &scene;
    fd.pipelines = &pipelines;
    fd.resources = &res;
    fd.materials = &matTex;
    fd.linearSamplerHandle = linearSamplerHandle;
    fd.shadowSamplerHandle = res.shadowSampler;
    fd.lightCount = kMaxPointLights;
    memcpy(fd.dirLightDir, dirLightDir, sizeof(dirLightDir));
    memcpy(fd.dirLightColor, dirLightColor, sizeof(dirLightColor));
    memcpy(fd.ambientColor, ambientColor, sizeof(ambientColor));
    fd.dirLightIntensity = dirLightIntensity;
    fd.surfaceWidth = 1280;
    fd.surfaceHeight = 720;
    fd.frameIndex = 0;
    fd.rhiVertexBuffer = rhiVB;
    fd.rhiIndexBuffer = rhiIB;

    bool keys[256] = {};
    bool rmd = false;
    bool quit = false;
    uint32_t frame = 0;

    auto last = std::chrono::steady_clock::now();
    auto lastPrint = last;

    printf("WASD move, Q/E up/down, right-drag look, IJKL look, Esc quit.\n");
    printf("Cascaded shadows: 4x%ux%u, Point lights: %u, SSGI: %ux%u half-res\n",
           kShadowMapSize, kShadowMapSize, kMaxPointLights, 1280/2, 720/2);

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
                recreateDepth(res, fd.surfaceWidth, fd.surfaceHeight);
            }
            if (ev.type == GPU_PLATFORM_EVENT_KEY_DOWN || ev.type == GPU_PLATFORM_EVENT_KEY_UP) {
                uint32_t k = ev.key.keycode; bool dn = ev.type == GPU_PLATFORM_EVENT_KEY_DOWN;
                if (k >= 'A' && k <= 'Z') k += 'a' - 'A';
                if (k < 256) keys[k] = dn;
                if (dn && k == 27) quit = true;
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN || ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_UP) {
                if (ev.mouse.button == kMouseButtonRight) rmd = ev.type == GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN;
            }
            if (ev.type == GPU_PLATFORM_EVENT_MOUSE_MOVE && rmd) rotateCameraByMouse(cam, ev.mouse.dx, ev.mouse.dy);
        }
        updateCamera(cam, keys, dt);

        // Update per-frame data
        float t = (float)frame * 0.02f;
        fd.cameraParams = makeCameraParams(cam, (float)fd.surfaceWidth / fd.surfaceHeight);
        fd.frameIndex = frame;

        // Animate lights: upload new positions/colors
        {
            std::vector<PointLightData> lights(kMaxPointLights);
            for (uint32_t i = 0; i < kMaxPointLights; i++) {
                float angle = (float)i / (float)kMaxPointLights * 6.28318f + t * 0.2f;
                float angle2 = (float)(i * 7 % 773) / 773.0f * 6.28318f + t * 0.3f;
                float radius = 200.0f + (float)(i % 5) * 150.0f;
                float heightVar = sinf(angle2) * 150.0f;
                lights[i].position[0] = center.x + cosf(angle) * radius;
                lights[i].position[1] = center.y + 80.0f + heightVar;
                lights[i].position[2] = center.z + sinf(angle) * radius;
                lights[i].radius = 80.0f + (float)(i % 4) * 60.0f;
                float hue = (float)i / (float)kMaxPointLights;
                lights[i].color[0] = 0.5f + 0.5f * sinf(hue * 6.28318f + t * 0.5f);
                lights[i].color[1] = 0.5f + 0.5f * sinf((hue + 0.33f) * 6.28318f + t * 0.5f);
                lights[i].color[2] = 0.5f + 0.5f * sinf((hue + 0.67f) * 6.28318f + t * 0.5f);
            }
            gpuUploadToBuffer(device, res.lightBuffer, lights.data(), kMaxPointLights * sizeof(PointLightData), 0);
        }

        // Compute cascade shadow cameras
        {
            float nearZ = 3.0f, farZ = 6000.0f;
            float cascadeSplits[4] = {0.05f, 0.15f, 0.35f, 0.70f};

            for (int ci = 0; ci < 4; ci++) {
                float cn = nearZ + (farZ - nearZ) * (ci == 0 ? 0 : cascadeSplits[ci - 1]);
                float cf = nearZ + (farZ - nearZ) * cascadeSplits[ci];
                (void)cn; (void)cf;

                // Shadow camera looks at scene center from a direction
                float shadowDist = 300.0f + (float)ci * 400.0f;
                Vec3 shPos = {
                    center.x + sinf(0.5f) * shadowDist,
                    center.y + 400.0f + (float)ci * 200.0f,
                    center.z + cosf(0.5f) * shadowDist
                };
                float shadowFov = 30.0f + (float)ci * 15.0f;
                fd.shadowCameras[ci] = makeCameraLookAt(shPos, center, shadowFov, 1.0f, 10.0f, 4000.0f + (float)ci * 2000.0f);
            }
        }

        // Acquire backbuffer
        GpuSurfaceTexture bb = nullptr;
        if (gpuSurfaceAcquireNextImage(surface, &bb) != GPU_SUCCESS) continue;

        // ---- Build and execute render graph ----
        GpuGraph graph = nullptr;
        if (gpuGraphCreate(device, &graph) != GPU_SUCCESS) { gpuSurfaceTextureRelease(bb); continue; }

        GpuGraphResource bbRes = gpuGraphImportSurfaceTexture(graph, bb, "backbuffer");
        GpuGraphResource depthRes = gpuGraphImportTexture(graph, res.sceneDepth, GPU_RESOURCE_STATE_DEPTH_WRITE, "scene_depth");

        // Import cascade depth textures
        GpuGraphResource cascadeRes[4];
        for (int ci = 0; ci < 4; ci++) {
            char name[32]; snprintf(name, sizeof(name), "cascade_%d", ci);
            cascadeRes[ci] = gpuGraphImportTexture(graph, res.cascadeDepth[ci], GPU_RESOURCE_STATE_DEPTH_WRITE, name);
        }

        // Import SSGI textures as read/write resources
        GpuGraphResource ssgiRes = gpuGraphImportTexture(graph, res.ssgiOutput, GPU_RESOURCE_STATE_UNORDERED_ACCESS, "ssgi_output");

        // Import light buffer
        GpuGraphResource lightBufRes = gpuGraphImportBuffer(graph, res.lightBuffer, GPU_RESOURCE_STATE_COPY_DEST, "light_buffer");

        // ---- CSM shadow passes (4 cascades) ----
        for (int ci = 0; ci < 4; ci++) {
            char pn[32]; snprintf(pn, sizeof(pn), "shadow_cascade_%d", ci);
            GpuGraphPass sp = gpuGraphAddRenderPass(graph, pn);
            GpuGraphDepthAttachment sda = {};
            sda.resource = cascadeRes[ci];
            sda.loadOp = GPU_LOAD_OP_CLEAR;
            sda.storeOp = GPU_STORE_OP_STORE;
            sda.clearDepth = 1.0f;
            gpuGraphPassSetDepthAttachment(sp, &sda);
            gpuGraphPassSetCallback(sp, shadowPassCallback, &fd);
        }

        // ---- Light cull compute pass ----
        GpuGraphPass lcp = gpuGraphAddComputePass(graph, "light_cull");
        gpuGraphPassReadWrite(lcp, lightBufRes);
        gpuGraphPassSetCallback(lcp, lightCullPassCallback, &fd);

        // ---- SSGI compute pass ----
        GpuGraphPass ssp = gpuGraphAddComputePass(graph, "ssgi");
        gpuGraphPassRead(ssp, depthRes);
        gpuGraphPassReadWrite(ssp, ssgiRes);
        gpuGraphPassSetCallback(ssp, ssgiPassCallback, &fd);

        // ---- Forward pass ----
        GpuGraphPass fp = gpuGraphAddRenderPass(graph, "forward_pass");
        GpuGraphColorAttachment ca = {};
        ca.resource = bbRes;
        ca.loadOp = GPU_LOAD_OP_CLEAR;
        ca.storeOp = GPU_STORE_OP_STORE;
        ca.clearColor[0] = 0.025f; ca.clearColor[1] = 0.030f; ca.clearColor[2] = 0.040f; ca.clearColor[3] = 1.0f;
        gpuGraphPassSetColorAttachments(fp, 1, &ca);

        GpuGraphDepthAttachment da = {};
        da.resource = depthRes;
        da.loadOp = GPU_LOAD_OP_CLEAR;
        da.storeOp = GPU_STORE_OP_STORE;
        da.clearDepth = 1.0f;
        gpuGraphPassSetDepthAttachment(fp, &da);

        // Forward pass reads cascade shadows + SSGI + lights
        for (int ci = 0; ci < 4; ci++) gpuGraphPassRead(fp, cascadeRes[ci]);
        gpuGraphPassRead(fp, ssgiRes);
        gpuGraphPassRead(fp, lightBufRes);
        gpuGraphPassSetCallback(fp, forwardPassCallback, &fd);

        // Compile and execute
        if (gpuGraphCompile(graph) != GPU_SUCCESS) {
            printf("Graph compile failed\n");
            gpuGraphDestroy(graph);
            gpuSurfaceTextureRelease(bb);
            continue;
        }

        GpuCommandQueue queue = nullptr;
        gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
        gpuGraphExecute(graph, queue);
        gpuGraphDestroy(graph);

        gpuSurfacePresent(surface);
        gpuSurfaceTextureRelease(bb);

        frame++;
        if (maxFrames > 0 && frame >= maxFrames) quit = true;

        if (std::chrono::duration<float>(now - lastPrint).count() > 1.0f) {
            printf("pos=(%.0f,%.0f,%.0f) yaw=%.2f frame=%u lt=%u\r",
                   cam.position.x, cam.position.y, cam.position.z, cam.yaw, frame, kMaxPointLights);
            fflush(stdout);
            lastPrint = now;
        }
    }

    device->graphicsQueue->waitOnHost();

    // Cleanup
    destroyDemoPipelines(device, pipelines);
    destroyRenderResources(res);
    destroySponzaMaterialTextures(device, matTex);
    gpuSurfaceUnconfigure(surface);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();

    printf("\nRendered %u frames.\n", frame);
    return 0;
}

// ============================================================================
// Render graph pass callbacks (forward declared above)
// ============================================================================

static void setCommonRenderState(GpuGraphPassContext* ctx, FrameData* d) {
    if (!ctx->renderPass) return;
    RenderState st = {};
    st.viewports[0] = Viewport::fromSize((float)d->surfaceWidth, (float)d->surfaceHeight);
    st.viewportCount = 1;
    st.scissorRects[0] = ScissorRect::fromSize(d->surfaceWidth, d->surfaceHeight);
    st.scissorRectCount = 1;
    st.vertexBuffers[0] = d->rhiVertexBuffer;
    st.vertexBufferCount = 1;
    st.indexBuffer = d->rhiIndexBuffer;
    st.indexFormat = IndexFormat::Uint32;
    ctx->renderPass->rhiPassEncoder->setRenderState(st);
}

static void shadowPassCallback(GpuGraphPassContext* ctx, void* userData) {
    FrameData* d = (FrameData*)userData;
    if (!ctx->renderPass || !d->pipelines->shadowPipeline || !d->pipelines->shadowRootObj) return;

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;

    // Set cascade cameras into shadow root shader object
    {
        ShaderCursor c(d->pipelines->shadowRootObj);
        // shadow.slang declares: CascadeUniforms cascadeData;
        // CascadeUniforms has: ShadowCameraParams cascades[4];
        for (int ci = 0; ci < 4; ci++) {
            c["cascadeData"]["cascades"][ci].setData(d->shadowCameras[ci]);
        }
    }

    rpEnc->bindPipeline(d->pipelines->shadowPipeline, d->pipelines->shadowRootObj);

    RenderState st = {};
    st.viewports[0] = Viewport::fromSize((float)kShadowMapSize, (float)kShadowMapSize);
    st.viewportCount = 1;
    st.scissorRects[0] = ScissorRect::fromSize(kShadowMapSize, kShadowMapSize);
    st.scissorRectCount = 1;
    st.vertexBuffers[0] = d->rhiVertexBuffer;
    st.vertexBufferCount = 1;
    st.indexBuffer = d->rhiIndexBuffer;
    st.indexFormat = IndexFormat::Uint32;
    rpEnc->setRenderState(st);

    for (const auto& draw : d->scene->draws) {
        DrawArguments a = {};
        a.vertexCount = draw.indexCount;
        a.instanceCount = 4; // 4 cascades via instancing
        a.startIndexLocation = draw.firstIndex;
        rpEnc->drawIndexed(a);
    }
}

static void forwardPassCallback(GpuGraphPassContext* ctx, void* userData) {
    FrameData* d = (FrameData*)userData;
    if (!ctx->renderPass || !d->pipelines->forwardPipeline || !d->pipelines->forwardRootObj) return;

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;

    // Update forward root shader object
    {
        ShaderCursor c(d->pipelines->forwardRootObj);
        c["camera"].setData(d->cameraParams);
        c["pointLightCount"].setData(d->lightCount);
        c["dirLightDir"].setData(d->dirLightDir);
        c["dirLightColor"].setData(d->dirLightColor);
        c["dirLightIntensity"].setData(d->dirLightIntensity);
        c["ambientColor"].setData(d->ambientColor);
        c["screenWidth"].setData(d->surfaceWidth);
        c["screenHeight"].setData(d->surfaceHeight);

        // Bind base color texture array + linear sampler (CRITICAL: without these albedo=black)
        auto* baseColorView = d->device->textureViewPool.resolve(
            d->materials->baseColorView.index,
            d->materials->baseColorView.generation);
        if (baseColorView) c["baseColorArray"].setBinding(baseColorView);
        auto* linSamp = d->device->samplerPool.resolve(
            d->resources->linearSampler.index,
            d->resources->linearSampler.generation);
        if (linSamp) c["linearSampler"].setBinding(linSamp);

        // Bind shadow comparison sampler
        auto* shdSamp = d->device->samplerPool.resolve(
            d->resources->shadowSampler.index,
            d->resources->shadowSampler.generation);
        if (shdSamp) c["shadowSampler"].setBinding(shdSamp);

        // Bind cascade shadow maps, matrices, and cascade data
        for (int ci = 0; ci < 4; ci++) {
            auto* srv = d->device->textureViewPool.resolve(
                d->resources->cascadeSRV[ci].index,
                d->resources->cascadeSRV[ci].generation);
            if (srv) c["shadowMaps"][ci].setBinding(srv);

            // Compute view-projection matrix for this cascade
            float viewMat[16], projMat[16];
            buildViewMatrix(d->shadowCameras[ci], viewMat);
            buildProjMatrix(d->shadowCameras[ci], projMat);
            float result[16] = {};
            for (int r = 0; r < 4; r++)
                for (int c2 = 0; c2 < 4; c2++)
                    for (int k = 0; k < 4; k++)
                        result[r * 4 + c2] += projMat[r * 4 + k] * viewMat[k * 4 + c2];
            c["cascadeViewProj"][ci].setData(result);

            // Set cascade data (texelSize etc.)
            ShadowCascadeData cd = {};
            cd.splitDepth = 0.0f;
            cd.texelSize = 1.0f / (float)kShadowMapSize;
            c["cascades"][ci].setData(cd);
        }

        // Bind SSGI output + sampler
        auto* ssgiSrv = d->device->textureViewPool.resolve(
            d->resources->ssgiOutputSrv.index,
            d->resources->ssgiOutputSrv.generation);
        if (ssgiSrv) c["ssgiTexture"].setBinding(ssgiSrv);
        if (linSamp) c["ssgiSampler"].setBinding(linSamp);

        // Bind light buffer as structured buffer
        auto* lightBuf = d->device->bufferPool.resolve(
            d->resources->lightBuffer.index,
            d->resources->lightBuffer.generation);
        if (lightBuf) c["pointLights"].setBinding(lightBuf);
    }

    rpEnc->bindPipeline(d->pipelines->forwardPipeline, d->pipelines->forwardRootObj);
    setCommonRenderState(ctx, d);

    for (const auto& draw : d->scene->draws) {
        DrawArguments a = {};
        a.vertexCount = draw.indexCount;
        a.instanceCount = 1;
        a.startIndexLocation = draw.firstIndex;
        rpEnc->drawIndexed(a);
    }
}

static void ssgiPassCallback(GpuGraphPassContext* ctx, void* userData) {
    FrameData* d = (FrameData*)userData;
    if (!ctx->computePass || !d->pipelines->ssgiPipeline || !d->pipelines->ssgiRootObj) return;
    auto* cpEnc = reinterpret_cast<IComputePassEncoder*>(ctx->computePass);

    // Bind SSGI shader parameters
    {
        ShaderCursor c(d->pipelines->ssgiRootObj);
        c["camera"].setData(d->cameraParams);

        // Build SSGIParams
        SSGIParams sp = {};
        float viewMatrix[16], invViewMatrix[16], projMatrix[16], invProjMatrix[16];
        buildViewMatrix(d->cameraParams, viewMatrix);
        buildProjMatrix(d->cameraParams, projMatrix);
        // Inverse view = rotation^T + translation
        {
            float tx = d->cameraParams.cameraPos[0];
            float ty = d->cameraParams.cameraPos[1];
            float tz = d->cameraParams.cameraPos[2];
            memset(invViewMatrix, 0, sizeof(float) * 16);
            invViewMatrix[0] = d->cameraParams.right[0];
            invViewMatrix[1] = d->cameraParams.up[0];
            invViewMatrix[2] = -d->cameraParams.forward[0];
            invViewMatrix[4] = d->cameraParams.right[1];
            invViewMatrix[5] = d->cameraParams.up[1];
            invViewMatrix[6] = -d->cameraParams.forward[1];
            invViewMatrix[8] = d->cameraParams.right[2];
            invViewMatrix[9] = d->cameraParams.up[2];
            invViewMatrix[10] = -d->cameraParams.forward[2];
            invViewMatrix[3] = -(invViewMatrix[0]*tx + invViewMatrix[4]*ty + invViewMatrix[8]*tz);
            invViewMatrix[7] = -(invViewMatrix[1]*tx + invViewMatrix[5]*ty + invViewMatrix[9]*tz);
            invViewMatrix[11] = -(invViewMatrix[2]*tx + invViewMatrix[6]*ty + invViewMatrix[10]*tz);
            invViewMatrix[15] = 1.0f;
        }
        // Inverse proj for custom projection
        {
            float fx = d->cameraParams.proj[0], fy = d->cameraParams.proj[1];
            float n = d->cameraParams.zParams[0], f = d->cameraParams.zParams[1];
            memset(invProjMatrix, 0, sizeof(float) * 16);
            invProjMatrix[0] = 1.0f / fx;
            invProjMatrix[5] = 1.0f / fy;
            invProjMatrix[11] = -(f - n) / (f * n);
            invProjMatrix[14] = 1.0f;
            invProjMatrix[15] = f * n / (f - n);
        }
        memcpy(sp.projMatrix, projMatrix, sizeof(float) * 16);
        memcpy(sp.invProjMatrix, invProjMatrix, sizeof(float) * 16);
        memcpy(sp.viewMatrix, viewMatrix, sizeof(float) * 16);
        memcpy(sp.invViewMatrix, invViewMatrix, sizeof(float) * 16);
        sp.cameraPos[0] = d->cameraParams.cameraPos[0];
        sp.cameraPos[1] = d->cameraParams.cameraPos[1];
        sp.cameraPos[2] = d->cameraParams.cameraPos[2];
        sp.stepSize = 0.5f;
        sp.maxDistance = 50.0f;
        sp.screenWidth = d->surfaceWidth / 2;
        sp.screenHeight = d->surfaceHeight / 2;
        sp.temporalFrame = d->frameIndex;
        c["ssgiParams"].setData(sp);

        // Bind depth SRV (view, not raw texture)
        auto* depthSrv = d->device->textureViewPool.resolve(
            d->resources->sceneDepthSrv.index,
            d->resources->sceneDepthSrv.generation);
        if (depthSrv) c["depthTexture"].setBinding(depthSrv);

        auto* samp = d->device->samplerPool.resolve(
            d->resources->linearSampler.index,
            d->resources->linearSampler.generation);
        if (samp) c["depthSampler"].setBinding(samp);

        // Bind SSGI output as UAV view
        auto* outUav = d->device->textureViewPool.resolve(
            d->resources->ssgiOutputUav.index,
            d->resources->ssgiOutputUav.generation);
        if (outUav) c["outputTexture"].setBinding(outUav);
    }
    cpEnc->bindPipeline(d->pipelines->ssgiPipeline, d->pipelines->ssgiRootObj);
    cpEnc->dispatchCompute((d->surfaceWidth + 15) / 16, (d->surfaceHeight + 15) / 16, 1);
}

static void lightCullPassCallback(GpuGraphPassContext* ctx, void* userData) {
    FrameData* d = (FrameData*)userData;
    if (!ctx->computePass || !d->pipelines->lightCullPipeline) return;
    auto* cpEnc = reinterpret_cast<IComputePassEncoder*>(ctx->computePass);
    cpEnc->bindPipeline(d->pipelines->lightCullPipeline);
    uint32_t tilesX = (d->surfaceWidth + 15) / 16;
    uint32_t tilesY = (d->surfaceHeight + 15) / 16;
    cpEnc->dispatchCompute(tilesX, tilesY, 1);
}
