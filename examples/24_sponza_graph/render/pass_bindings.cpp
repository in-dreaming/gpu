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

static bool bindShadowResources(rhi::ShaderCursor cursor, const FrameData& frame, bool logFailures)
{
    if (!frame.device || !frame.resources) return false;
    const RenderResources& res = *frame.resources;
    const char* mapNames[] = {"shadowMap0", "shadowMap1", "shadowMap2", "shadowMap3"};
    bool ok = true;
    for (int i = 0; i < 4; i++) {
        auto* view = static_cast<rhi::ITextureView*>(frame.device->textureViewPool.resolve(
            res.cascadeSRV[i].index, res.cascadeSRV[i].generation));
        ok = shaderCursorSetBinding(cursor, mapNames[i], view, logFailures ? mapNames[i] : nullptr) && ok;
    }
    auto* sampler = static_cast<rhi::ISampler*>(frame.device->samplerPool.resolve(
        res.shadowSampler.index, res.shadowSampler.generation));
    ok = shaderCursorSetBinding(cursor, "shadowSampler", sampler, logFailures ? "shadowSampler" : nullptr) && ok;
    return ok;
}

static bool bindPointShadowResources(rhi::ShaderCursor cursor, const FrameData& frame, bool logFailures)
{
    if (!frame.device || !frame.resources) return false;
    const RenderResources& res = *frame.resources;
    const char* cubeNames[] = {
        "pointShadowCube0", "pointShadowCube1", "pointShadowCube2", "pointShadowCube3",
        "pointShadowCube4", "pointShadowCube5", "pointShadowCube6", "pointShadowCube7",
    };
    bool ok = true;
    for (uint32_t i = 0; i < kMaxPointShadowSlots; i++) {
        auto* view = static_cast<rhi::ITextureView*>(frame.device->textureViewPool.resolve(
            res.pointShadowCubeSRV[i].index, res.pointShadowCubeSRV[i].generation));
        ok = shaderCursorSetBinding(cursor, cubeNames[i], view, logFailures ? cubeNames[i] : nullptr) && ok;
    }
    auto* sampler = static_cast<rhi::ISampler*>(frame.device->samplerPool.resolve(
        res.pointShadowDepthSampler.index, res.pointShadowDepthSampler.generation));
    ok = shaderCursorSetBinding(cursor, "pointShadowDepthSampler", sampler,
                                logFailures ? "pointShadowDepthSampler" : nullptr) &&
         ok;
    return ok;
}

static bool bindPointLightBuffer(rhi::ShaderCursor cursor, const FrameData& frame, bool logFailures)
{
    if (!frame.device || !frame.resources) return false;
    auto* buf = static_cast<rhi::IBuffer*>(frame.device->bufferPool.resolve(
        frame.resources->lightBuffer.index, frame.resources->lightBuffer.generation));
    return shaderCursorSetBinding(cursor, "pointLights", buf, logFailures ? "pointLights" : nullptr);
}

static bool bindSsgiPassCursorResources(rhi::ShaderCursor cursor, const FrameData& frame, bool logFailures)
{
    if (!frame.device || !frame.resources) return false;
    const RenderResources& res = *frame.resources;
    bool ok = true;
    auto* albedo = static_cast<rhi::ITextureView*>(frame.device->textureViewPool.resolve(
        res.sceneAlbedoSrv.index, res.sceneAlbedoSrv.generation));
    auto* normal = static_cast<rhi::ITextureView*>(frame.device->textureViewPool.resolve(
        res.sceneNormalSrv.index, res.sceneNormalSrv.generation));
    auto* depth = static_cast<rhi::ITextureView*>(frame.device->textureViewPool.resolve(
        res.sceneDepthSrv.index, res.sceneDepthSrv.generation));
    auto* output = static_cast<rhi::ITextureView*>(frame.device->textureViewPool.resolve(
        res.ssgiOutputUav.index, res.ssgiOutputUav.generation));
    auto* sampler = static_cast<rhi::ISampler*>(frame.device->samplerPool.resolve(
        res.linearSampler.index, res.linearSampler.generation));
    ok = shaderCursorSetBinding(cursor, "albedoTexture", albedo, logFailures ? "albedoTexture" : nullptr) && ok;
    ok = shaderCursorSetBinding(cursor, "normalTexture", normal, logFailures ? "normalTexture" : nullptr) && ok;
    ok = shaderCursorSetBinding(cursor, "depthTexture", depth, logFailures ? "depthTexture" : nullptr) && ok;
    ok = shaderCursorSetBinding(cursor, "sceneSampler", sampler, logFailures ? "sceneSampler" : nullptr) && ok;
    ok = shaderCursorSetBinding(cursor, "outputTexture", output, logFailures ? "outputTexture" : nullptr) && ok;
    if (frame.resources->lightBuffer.index)
        ok = bindPointLightBuffer(cursor, frame, logFailures) && ok;
    return ok;
}

static bool bindForwardPassResources(rhi::IShaderObject* root, const FrameData& frame, bool logFailures)
{
    if (!root) return false;
    ShaderCursor c(root);
    bool ok = true;

    // Graphics pass: direct RHI binding (same as 21_sponza_data_driven). Bindless heap still validates descriptors.
    ok = bindBaseColorArrayBindings(c, frame, logFailures) && ok;
    ok = bindShadowResources(c, frame, logFailures) && ok;
    if (frame.features.pointShadows)
        ok = bindPointShadowResources(c, frame, logFailures) && ok;

    if (frame.features.pointLights)
        ok = bindPointLightBuffer(c, frame, logFailures) && ok;
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
    ShaderCursor c(root);
    return bindSsgiPassCursorResources(c, frame, logFailures);
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

bool bindForwardFrameResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures)
{
    if (!pipelines.forwardRootObj) return false;
    ShaderCursor c(pipelines.forwardRootObj.get());
    bool ok = bindBaseColorArrayBindings(c, frame, logFailures);
    ok = bindShadowResources(c, frame, logFailures) && ok;
    if (frame.features.pointShadows)
        ok = bindPointShadowResources(c, frame, logFailures) && ok;
    if (frame.features.pointLights)
        ok = bindPointLightBuffer(c, frame, logFailures) && ok;
    return ok;
}

bool bindGbufferFrameResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures)
{
    return bindGbufferPassResources(pipelines.gbufferRootObj.get(), frame, logFailures);
}

bool bindSsgiFrameResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures)
{
    if (!pipelines.ssgiRootObj) return false;
    ShaderCursor c(pipelines.ssgiRootObj.get());
    return bindSsgiPassCursorResources(c, frame, logFailures);
}

bool bindForwardShadowResources(DemoPipelines& pipelines, const FrameData& frame, bool logFailures)
{
    return bindForwardFrameResources(pipelines, frame, logFailures);
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
