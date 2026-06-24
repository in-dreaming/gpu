#include "pass_callbacks.h"
#include "render/frame_data.h"
#include "gpu/core/gpu_internal.h"

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

static bool updateForwardPassUniforms(ShaderCursor& c, FrameData* d)
{
    ShaderCursor fwd = c["gFwd"];
    if (!fwd.isValid()) return false;

    if (SLANG_FAILED(fwd["camera"].setData(d->cameraParams))) return false;

    ForwardFeatureFlagsCpu ff = {};
    ff.enableDirLight = d->features.dirLight ? 1u : 0u;
    ff.enableDirShadow = d->features.dirShadows ? 1u : 0u;
    ff.enablePointLights = d->features.pointLights ? 1u : 0u;
    ff.enableSSGI = d->features.ssgi ? 1u : 0u;
    ff.enableFog = d->features.fog ? 1u : 0u;
    ff.pointLightCount = d->features.pointLights ? d->features.pointLightCount : 0u;
    if (SLANG_FAILED(fwd["features"].setData(ff))) return false;

    uint32_t layerCount = d->materials ? d->materials->layerCount : 1u;
    if (SLANG_FAILED(fwd["baseColorLayerCount"].setData(layerCount))) return false;

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
        if (!d->features.dirShadows) continue;
        if (SLANG_FAILED(fwd["cascadeViewProj"][ci].setData(d->cascadeShadows[ci].viewProj))) return false;
        ForwardCascadeUniforms cd = {};
        cd.splitFar = d->cascadeShadows[ci].splitFar;
        cd.texelSize = d->cascadeShadows[ci].texelSize;
        if (SLANG_FAILED(fwd["cascades"][ci].setData(cd))) return false;
    }

    return true;
}

void gbufferPassCallback(GpuGraphPassContext* ctx, void* userData)
{
    FrameData* d = (FrameData*)userData;
    if (!ctx->renderPass || !d->pipelines->gbufferPipeline || !d->pipelines->gbufferRootObj) return;

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;
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

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;
    uint32_t shadowSize = (passData->type == ShadowPassType::Cascade) ? kShadowMapSize : kPointShadowMapSize;

    {
        ShaderCursor c(d->pipelines->shadowRootObj);
        if (passData->type == ShadowPassType::Cascade)
            c["shadowViewProj"].setData(d->cascadeShadows[passData->cascadeIndex].viewProj);
        else
            c["shadowViewProj"].setData(d->pointShadowViewProj[passData->pointShadowSlot][passData->cubeFace]);
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
        DrawArguments a = {};
        a.vertexCount = draw.indexCount;
        a.instanceCount = 1;
        a.startIndexLocation = draw.firstIndex;
        rpEnc->drawIndexed(a);
    }
}

void forwardPassCallback(GpuGraphPassContext* ctx, void* userData)
{
    FrameData* d = (FrameData*)userData;
    if (!ctx->renderPass || !d->pipelines->forwardPipeline || !d->pipelines->forwardRootObj) return;

    rhi::IRenderPassEncoder* rpEnc = ctx->renderPass->rhiPassEncoder;
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

    {
        ShaderCursor c(d->pipelines->ssgiRootObj);
        SSGIParams sp = {};
        sp.camera = d->cameraParams;
        sp.cameraPos[0] = d->cameraParams.cameraPos[0];
        sp.cameraPos[1] = d->cameraParams.cameraPos[1];
        sp.cameraPos[2] = d->cameraParams.cameraPos[2];
        sp.stepSize = 2.0f;
        sp.maxDistance = 120.0f;
        sp.thickness = 4.0f;
        sp.screenWidth = d->surfaceWidth / 2;
        sp.screenHeight = d->surfaceHeight / 2;
        sp.fullWidth = d->surfaceWidth;
        sp.fullHeight = d->surfaceHeight;
        sp.temporalFrame = d->frameIndex;
        c["gSSGI"].setData(sp);
    }

    cpEnc->bindPipeline(d->pipelines->ssgiPipeline, d->pipelines->ssgiRootObj);
    cpEnc->dispatchCompute(((d->surfaceWidth / 2) + 7) / 8, ((d->surfaceHeight / 2) + 7) / 8, 1);
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
