#include "pass_callbacks.h"
#include "render/frame_data.h"
#include "pass_bindings.h"
#include "render/bindless_bind.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/core/gpu_buffer.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstring>

using namespace rhi;

static void drawScene(rhi::IRenderPassEncoder* rpEnc, FrameData* d)
{
    for (const auto& draw : d->scene->draws) {
        DrawArguments a = {};
        a.vertexCount = draw.indexCount;
        a.instanceCount = 1;
        a.startIndexLocation = draw.firstIndex;
        rpEnc->drawIndexed(a);
    }
}

static void setCommonRenderState(GpuGraphPassContext* ctx, FrameData* d)
{
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

static bool bindCascadeMatrixBuffer(ShaderCursor& c, FrameData* d, const char* debugName)
{
    if (!d->device || !d->resources || !gpuHandleIsValid(d->resources->cascadeMatrixBuffer)) return false;
    auto* rhiBuf = static_cast<rhi::IBuffer*>(d->device->bufferPool.resolve(
        d->resources->cascadeMatrixBuffer.index, d->resources->cascadeMatrixBuffer.generation));
    return shaderCursorSetBinding(c, "cascadeViewProjs", rhiBuf, debugName);
}

static bool uploadCascadeMatrixBuffer(FrameData* d)
{
    if (!d->device || !d->resources || !gpuHandleIsValid(d->resources->cascadeMatrixBuffer)) return false;
    float matrices[16 * 4] = {};
    for (int ci = 0; ci < 4; ci++)
        memcpy(matrices + ci * 16, d->cascadeShadows[ci].viewProj, sizeof(float) * 16);
    return gpuUploadToBuffer(d->device, d->resources->cascadeMatrixBuffer, matrices, sizeof(matrices), 0) ==
           GPU_SUCCESS;
}

static bool uploadAndBindCascadeMatrices(ShaderCursor& c, FrameData* d)
{
    if (!uploadCascadeMatrixBuffer(d)) return false;
    return bindCascadeMatrixBuffer(c, d, d->diagShadow ? "cascadeViewProjs" : nullptr);
}

static bool bindPointShadowMatrixBuffer(ShaderCursor& c, FrameData* d, const char* debugName)
{
    if (!d->device || !d->resources || !gpuHandleIsValid(d->resources->pointShadowMatrixBuffer)) return false;
    auto* rhiBuf = static_cast<rhi::IBuffer*>(d->device->bufferPool.resolve(
        d->resources->pointShadowMatrixBuffer.index, d->resources->pointShadowMatrixBuffer.generation));
    return shaderCursorSetBinding(c, "pointShadowViewProjs", rhiBuf, debugName);
}

static bool uploadPointShadowMatrixBuffer(FrameData* d)
{
    if (!d->device || !d->resources || !gpuHandleIsValid(d->resources->pointShadowMatrixBuffer)) return false;
    float matrices[16 * kMaxPointShadowSlots * kCubeFaceCount] = {};
    for (uint32_t slot = 0; slot < kMaxPointShadowSlots; slot++) {
        for (int face = 0; face < kCubeFaceCount; face++)
            memcpy(matrices + (slot * kCubeFaceCount + face) * 16,
                   d->pointShadowViewProj[slot][face],
                   sizeof(float) * 16);
    }
    return gpuUploadToBuffer(d->device, d->resources->pointShadowMatrixBuffer, matrices, sizeof(matrices), 0) ==
           GPU_SUCCESS;
}

static bool uploadPointShadowUniforms(ShaderCursor& c, FrameData* d)
{
    if (!d->features.pointShadows)
        return true;
    if (!uploadPointShadowMatrixBuffer(d)) return false;
    if (!bindPointShadowMatrixBuffer(c, d, d->diagShadow ? "pointShadowViewProjs" : nullptr)) return false;

    float nearFar[8][4] = {};
    for (uint32_t slot = 0; slot < kMaxPointShadowSlots; slot++) {
        nearFar[slot][0] = d->pointShadowNear[slot];
        nearFar[slot][1] = d->pointShadowFar[slot];
    }
    if (SLANG_FAILED(c["gPointShadowNearFar"].setData(nearFar))) return false;
    return true;
}

static bool uploadCascadeUniforms(ShaderCursor& c, FrameData* d)
{
    if (!uploadAndBindCascadeMatrices(c, d)) return false;
    float splitFar[4] = {};
    float texelSize[4] = {};
    for (int ci = 0; ci < 4; ci++) {
        splitFar[ci] = d->cascadeShadows[ci].splitFar;
        texelSize[ci] = d->cascadeShadows[ci].texelSize;
    }
    if (SLANG_FAILED(c["gCascadeSplitFar"].setData(splitFar))) return false;
    if (SLANG_FAILED(c["gCascadeTexelSize"].setData(texelSize))) return false;
    float worldTexel[4] = {};
    float depthBias[4] = {};
    for (int ci = 0; ci < 4; ci++) {
        worldTexel[ci] = d->cascadeShadows[ci].worldTexelSize;
        depthBias[ci] = d->cascadeShadows[ci].depthBiasNdc;
    }
    if (SLANG_FAILED(c["gCascadeWorldTexel"].setData(worldTexel))) return false;
    if (SLANG_FAILED(c["gCascadeDepthBias"].setData(depthBias))) return false;
    return true;
}

static bool updateForwardPassUniforms(ShaderCursor& c, FrameData* d)
{
    ShaderCursor fwd = c["gFwd"];
    if (!fwd.isValid()) return false;

    if (SLANG_FAILED(fwd["camera"].setData(d->cameraParams))) return false;

    ForwardFeatureFlagsCpu ff = {};
    ff.enableDirLight = d->features.dirLight ? 1u : 0u;
    ff.enableDirShadow = d->features.dirShadows ? 1u : 0u;
    ff.enablePointLights = d->features.pointLights ? 1u : 0u;
    ff.enablePointShadows = d->features.pointShadows ? 1u : 0u;
    const bool useIndirectGi = d->features.ssgi || d->viewMode == RenderViewMode::Final ||
                               d->viewMode == RenderViewMode::SSGI;
    ff.enableSSGI = useIndirectGi ? 1u : 0u;
    ff.enableFog = d->features.fog ? 1u : 0u;
    ff.pointLightCount = d->features.pointLights ? d->features.pointLightCount : 0u;
    if (SLANG_FAILED(fwd["features"].setData(ff))) return false;

    uint32_t layerCount = d->materials ? d->materials->layerCount : 1u;
    if (SLANG_FAILED(fwd["baseColorLayerCount"].setData(layerCount))) return false;

    if (SLANG_FAILED(c["enableDirShadowFlag"].setData(d->features.dirShadows ? 1u : 0u))) return false;
    if (SLANG_FAILED(c["enableDirLightFlag"].setData(d->features.dirLight ? 1u : 0u))) return false;
    if (SLANG_FAILED(c["enablePointShadowFlag"].setData(d->features.pointShadows ? 1u : 0u))) return false;
    if (!uploadCascadeUniforms(c, d)) return false;
    if (!uploadPointShadowUniforms(c, d)) return false;
    if (d->diagShadow) {
        printf("[diag] cascade0 viewProj[0]=%.6f splitFar=%.1f\n",
               d->cascadeShadows[0].viewProj[0], d->cascadeShadows[0].splitFar);
    }

    ForwardLightingCpu lighting = {};
    memcpy(lighting.dirLightDir, d->dirLightDir, sizeof(lighting.dirLightDir));
    lighting.dirLightIntensity = d->dirLightIntensity;
    memcpy(lighting.dirLightColor, d->dirLightColor, sizeof(lighting.dirLightColor));
    memcpy(lighting.ambientColor, d->ambientColor, sizeof(lighting.ambientColor));
    if (SLANG_FAILED(fwd["lighting"].setData(lighting))) return false;

    if (SLANG_FAILED(fwd["screenWidth"].setData(d->surfaceWidth)) ||
        SLANG_FAILED(fwd["screenHeight"].setData(d->surfaceHeight)) ||
        SLANG_FAILED(fwd["debugViewMode"].setData((uint32_t)d->viewMode))) {
        return false;
    }

    for (int ci = 0; ci < 4; ci++) {
        ForwardCascadeUniforms cd = {};
        cd.splitFar = d->cascadeShadows[ci].splitFar;
        cd.texelSize = d->cascadeShadows[ci].texelSize;
        if (SLANG_FAILED(fwd["cascades"][ci].setData(cd))) return false;
    }

    if (d->diagShadow) d->diagForwardUniformOk = true;
    return true;
}

void gbufferPassCallback(GpuGraphPassContext* ctx, void* userData)
{
    FrameData* d = (FrameData*)userData;
    if (!ctx->renderPass || !d->pipelines->gbufferPipeline || !d->pipelines->gbufferRootObj) return;

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;
    if (!bindGbufferFrameResources(*d->pipelines, *d, false)) return;
    {
        ShaderCursor c(d->pipelines->gbufferRootObj);
        c["camera"].setData(d->cameraParams);
        c["baseColorLayerCount"].setData(d->materials ? d->materials->layerCount : 1u);
    }

    rpEnc->bindPipeline(d->pipelines->gbufferPipeline, d->pipelines->gbufferRootObj);
    setCommonRenderState(ctx, d);
    drawScene(rpEnc, d);
}

void shadowPassCallback(GpuGraphPassContext* ctx, void* userData)
{
    ShadowPassData* passData = (ShadowPassData*)userData;
    FrameData* d = passData ? passData->frame : nullptr;
    if (!d || !ctx->renderPass || !d->pipelines->shadowPipeline || !d->pipelines->shadowRootObj) return;

    if (d->diagShadow) d->diagShadowPasses++;

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;
    uint32_t shadowSize = (passData->type == ShadowPassType::Cascade) ? kShadowMapSize : kPointShadowMapSize;

    {
        ShaderCursor c(d->pipelines->shadowRootObj);
        if (passData->type == ShadowPassType::Cascade) {
            if (!uploadCascadeMatrixBuffer(d)) return;
            if (!bindCascadeMatrixBuffer(c, d, d->diagShadow ? "shadowCascadeViewProjs" : nullptr)) return;
            if (SLANG_FAILED(c["shadowCascadeIndex"].setData((uint32_t)passData->cascadeIndex))) return;
        } else {
            const int slot = passData->pointShadowSlot;
            if (SLANG_FAILED(c["shadowCascadeIndex"].setData(4u))) return;
            c["shadowViewProj"].setData(d->pointShadowViewProj[slot][passData->cubeFace]);
            float lightPos[3] = {d->pointShadowLightPos[slot][0], d->pointShadowLightPos[slot][1],
                                 d->pointShadowLightPos[slot][2]};
            if (SLANG_FAILED(c["pointShadowLightPos"].setData(lightPos))) return;
            float nearFar[2] = {d->pointShadowNear[slot], d->pointShadowFar[slot]};
            if (SLANG_FAILED(c["pointShadowNearFar"].setData(nearFar))) return;
        }
    }

    rpEnc->bindPipeline(d->pipelines->shadowPipeline, d->pipelines->shadowRootObj);

    RenderState st = {};
    st.viewports[0] = Viewport::fromSize((float)shadowSize, (float)shadowSize);
    st.viewportCount = 1;
    st.scissorRects[0] = ScissorRect::fromSize(shadowSize, shadowSize);
    st.scissorRectCount = 1;
    st.vertexBuffers[0] = d->rhiVertexBuffer;
    st.vertexBufferCount = 1;
    st.indexBuffer = d->rhiIndexBuffer;
    st.indexFormat = IndexFormat::Uint32;
    rpEnc->setRenderState(st);

    for (const auto& draw : d->scene->draws) {
        if (!draw.castsShadow) continue;
        DrawArguments a = {};
        a.vertexCount = draw.indexCount;
        a.instanceCount = 1;
        a.startIndexLocation = draw.firstIndex;
        rpEnc->drawIndexed(a);
        if (d->diagShadow) d->diagShadowDraws++;
    }
}

void forwardPassCallback(GpuGraphPassContext* ctx, void* userData)
{
    FrameData* d = (FrameData*)userData;
    if (!ctx->renderPass || !d->pipelines->forwardPipeline || !d->pipelines->forwardRootObj) return;

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;
    if (!bindForwardShadowResources(*d->pipelines, *d, false)) return;
    {
        ShaderCursor c(d->pipelines->forwardRootObj);
        if (!updateForwardPassUniforms(c, d)) return;
    }

    rpEnc->bindPipeline(d->pipelines->forwardPipeline, d->pipelines->forwardRootObj);
    setCommonRenderState(ctx, d);
    drawScene(rpEnc, d);
}

void ssgiPassCallback(GpuGraphPassContext* ctx, void* userData)
{
    FrameData* d = (FrameData*)userData;
    if (!ctx->computePass || !d->pipelines->ssgiPipeline || !d->pipelines->ssgiRootObj) return;
    auto* cpEnc = reinterpret_cast<IComputePassEncoder*>(ctx->computePass);

    if (!bindSsgiFrameResources(*d->pipelines, *d, false)) return;

    {
        ShaderCursor c(d->pipelines->ssgiRootObj);
        SSGIParams sp = {};
        sp.camera = d->cameraParams;
        sp.cameraPos[0] = d->cameraParams.cameraPos[0];
        sp.cameraPos[1] = d->cameraParams.cameraPos[1];
        sp.cameraPos[2] = d->cameraParams.cameraPos[2];
        sp.stepSize = d->demoScene && d->demoScene->isCompactRoom() ? 0.75f : 1.5f;
        sp.maxDistance = d->demoScene && d->demoScene->isCompactRoom() ? 120.0f : 160.0f;
        sp.thickness = 4.5f;
        sp.normalBias = d->demoScene && d->demoScene->isCompactRoom() ? 0.25f : 0.15f;
        sp.indirectBoost = 1.8f;
        sp.screenWidth = d->surfaceWidth;
        sp.screenHeight = d->surfaceHeight;
        sp.fullWidth = d->surfaceWidth;
        sp.fullHeight = d->surfaceHeight;
        sp.temporalFrame = d->frameIndex;
        sp.enablePointLights = d->features.pointLights ? 1u : 0u;
        sp.pointLightCount = d->features.pointLights ? d->features.pointLightCount : 0u;
        sp.ambientColor[0] = d->ambientColor[0];
        sp.ambientColor[1] = d->ambientColor[1];
        sp.ambientColor[2] = d->ambientColor[2];
        c["gSSGI"].setData(sp);
    }

    cpEnc->bindPipeline(d->pipelines->ssgiPipeline, d->pipelines->ssgiRootObj);
    cpEnc->dispatchCompute((d->surfaceWidth + 7) / 8, (d->surfaceHeight + 7) / 8, 1);
}

void lightCullPassCallback(GpuGraphPassContext* ctx, void* userData)
{
    FrameData* d = (FrameData*)userData;
    if (!ctx->computePass || !d->pipelines->lightCullPipeline || !d->pipelines->lightCullRootObj) return;
    auto* cpEnc = reinterpret_cast<IComputePassEncoder*>(ctx->computePass);

    {
        ShaderCursor c(d->pipelines->lightCullRootObj);
        LightCullParams lp = {};
        lp.screenWidth = d->surfaceWidth;
        lp.screenHeight = d->surfaceHeight;
        lp.lightCount = d->lightCount;
        lp.tileSize = 16;
        lp.camera = d->cameraParams;
        c["gCull"].setData(lp);
    }

    uint32_t tw = (d->surfaceWidth + 15) / 16;
    uint32_t th = (d->surfaceHeight + 15) / 16;
    cpEnc->bindPipeline(d->pipelines->lightCullPipeline, d->pipelines->lightCullRootObj);
    cpEnc->dispatchCompute(tw, th, 1);
}
