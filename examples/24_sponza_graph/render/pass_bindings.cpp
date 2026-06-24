#include "pass_bindings.h"
#include "render/bindless_bind.h"
#include "render/frame_data.h"

#include <cstdio>

using namespace rhi;

static bool bindBaseColorArrayBindings(rhi::ShaderCursor cursor, const FrameData& frame, bool logFailures)
{
    if (!frame.device || !frame.materials || !frame.resources) return false;
    const char* texName = logFailures ? "baseColorArray" : nullptr;
    const char* sampName = logFailures ? "linearSampler" : nullptr;
    auto* view = static_cast<rhi::ITextureView*>(frame.device->textureViewPool.resolve(
        frame.materials->baseColorView.index, frame.materials->baseColorView.generation));
    auto* sampler = static_cast<rhi::ISampler*>(frame.device->samplerPool.resolve(
        frame.resources->linearSampler.index, frame.resources->linearSampler.generation));
    bool ok = true;
    ok = shaderCursorSetBinding(cursor, "baseColorArray", view, texName) && ok;
    ok = shaderCursorSetBinding(cursor, "linearSampler", sampler, sampName) && ok;
    return ok;
}

static bool bindForwardPassResources(rhi::IShaderObject* root, const FrameData& frame, bool logFailures)
{
    if (!root) return false;
    const RenderResources& res = *frame.resources;
    ShaderCursor c(root);
    bool ok = true;

    // Graphics pass: direct RHI binding (same as 21_sponza_data_driven). Bindless heap still validates descriptors.
    ok = bindBaseColorArrayBindings(c, frame, logFailures) && ok;

    const char* shadowNames[] = {"shadowMap0", "shadowMap1", "shadowMap2", "shadowMap3"};
    for (int i = 0; i < 4; i++) {
        ok = shaderCursorSetBindlessHeap(c, shadowNames[i], res.textureBindlessHeap, res.bindless.cascadeShadows[i],
                                           logFailures ? shadowNames[i] : nullptr) && ok;
    }

    ok = shaderCursorSetBindlessHeap(c, "shadowSampler", res.samplerBindlessHeap, res.bindless.shadowSampler,
                                       logFailures ? "shadowSampler" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "pointLights", res.bufferBindlessHeap, res.bindless.lightBuffer,
                                       logFailures ? "pointLights" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "ssgiTexture", res.textureBindlessHeap, res.bindless.ssgiTexture,
                                       logFailures ? "ssgiTexture" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "ssgiSampler", res.samplerBindlessHeap, res.bindless.linearSampler,
                                       logFailures ? "ssgiSampler" : nullptr) && ok;
    return ok;
}

static bool bindGbufferPassResources(rhi::IShaderObject* root, const FrameData& frame, bool logFailures)
{
    if (!root) return false;
    ShaderCursor c(root);
    bool ok = true;
    ok = bindBaseColorArrayBindings(c, frame, logFailures) && ok;
    return ok;
}

static bool bindSsgiPassResources(rhi::IShaderObject* root, const FrameData& frame, bool logFailures)
{
    if (!root) return false;
    const RenderResources& res = *frame.resources;
    ShaderCursor c(root);
    bool ok = true;
    ok = shaderCursorSetBindlessHeap(c, "albedoTexture", res.textureBindlessHeap, res.bindless.sceneAlbedo,
                                       logFailures ? "albedoTexture" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "normalTexture", res.textureBindlessHeap, res.bindless.sceneNormal,
                                       logFailures ? "normalTexture" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "depthTexture", res.textureBindlessHeap, res.bindless.sceneDepth,
                                       logFailures ? "depthTexture" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "sceneSampler", res.samplerBindlessHeap, res.bindless.linearSampler,
                                       logFailures ? "sceneSampler" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "outputTexture", res.textureBindlessHeap, res.bindless.ssgiOutputUav,
                                       logFailures ? "outputTexture" : nullptr) && ok;
    return ok;
}

static bool bindLightCullPassResources(rhi::IShaderObject* root, const FrameData& frame, bool logFailures)
{
    if (!root) return false;
    const RenderResources& res = *frame.resources;
    ShaderCursor c(root);
    bool ok = true;
    ok = shaderCursorSetBindlessHeap(c, "pointLights", res.bufferBindlessHeap, res.bindless.lightBuffer,
                                       logFailures ? "pointLights" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "lightIndexBuffer", res.bufferBindlessHeap, res.bindless.lightIndexBuffer,
                                       logFailures ? "lightIndexBuffer" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "depthTexture", res.textureBindlessHeap, res.bindless.sceneDepth,
                                       logFailures ? "depthTexture" : nullptr) && ok;
    ok = shaderCursorSetBindlessHeap(c, "depthSampler", res.samplerBindlessHeap, res.bindless.linearSampler,
                                       logFailures ? "depthSampler" : nullptr) && ok;
    return ok;
}

bool bindAllPassResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures)
{
    bool ok = true;
    ok = bindForwardPassResources(pipelines.forwardRootObj.get(), frame, logFailures) && ok;
    ok = bindGbufferPassResources(pipelines.gbufferRootObj.get(), frame, logFailures) && ok;
    if (pipelines.ssgiRootObj)
        ok = bindSsgiPassResources(pipelines.ssgiRootObj.get(), frame, logFailures) && ok;
    if (pipelines.lightCullRootObj)
        ok = bindLightCullPassResources(pipelines.lightCullRootObj.get(), frame, logFailures) && ok;
    return ok;
}
