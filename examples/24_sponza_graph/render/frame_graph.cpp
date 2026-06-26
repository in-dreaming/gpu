#include "frame_graph.h"
#include "pass_callbacks.h"

#include "core/shadow_math.h"
#include "core/types.h"
#include "gpu/core/gpu_command.h"

#include <algorithm>
#include <cstdio>

bool executeSponzaFrameGraph(const FrameGraphContext& ctx)
{
    if (!ctx.device || !ctx.backbuffer || !ctx.resources || !ctx.frame) return false;

    FrameData& fd = *ctx.frame;
    RenderResources& res = *ctx.resources;
    const RenderFeatures& features = ctx.features;

    GpuGraph graph = nullptr;
    if (gpuGraphCreate(ctx.device, &graph) != GPU_SUCCESS) return false;

    GpuGraphResource bbRes = gpuGraphImportSurfaceTexture(graph, ctx.backbuffer, "backbuffer");
    GpuGraphResource depthRes = gpuGraphImportTexture(graph, res.sceneDepth, GPU_RESOURCE_STATE_DEPTH_WRITE, "scene_depth");

    GpuGraphResource cascadeRes[4] = {};
    if (features.dirShadows) {
        for (int ci = 0; ci < 4; ci++) {
            char name[32];
            snprintf(name, sizeof(name), "cascade_%d", ci);
            cascadeRes[ci] = gpuGraphImportTexture(graph, res.cascadeDepth[ci], GPU_RESOURCE_STATE_DEPTH_WRITE, name);
        }
    }

    GpuGraphResource pointShadowRes[kMaxPointShadowSlots] = {};
    if (features.pointShadows) {
        for (uint32_t pi = 0; pi < kMaxPointShadowSlots; pi++) {
            char name[48];
            snprintf(name, sizeof(name), "point_shadow_cube_%u", pi);
            pointShadowRes[pi] =
                gpuGraphImportTexture(graph, res.pointShadowCube[pi], GPU_RESOURCE_STATE_DEPTH_WRITE, name);
        }
    }

    GpuGraphResource albedoRes = {};
    GpuGraphResource normalRes = {};
    if (features.gbuffer) {
        albedoRes = gpuGraphImportTexture(graph, res.sceneAlbedo, GPU_RESOURCE_STATE_RENDER_TARGET, "scene_albedo");
        normalRes = gpuGraphImportTexture(graph, res.sceneNormal, GPU_RESOURCE_STATE_RENDER_TARGET, "scene_normal");
    }

    GpuGraphResource ssgiRes = {};
    if (features.ssgi) {
        ssgiRes = gpuGraphImportTexture(graph, res.ssgiOutput, GPU_RESOURCE_STATE_UNORDERED_ACCESS, "ssgi_output");
    }

    GpuGraphResource lightBufRes = {};
    GpuGraphResource lightIndexBufRes = {};
    if (features.pointLights) {
        lightBufRes = gpuGraphImportBuffer(graph, res.lightBuffer, GPU_RESOURCE_STATE_COPY_DEST, "light_buffer");
    }
    if (features.lightCull) {
        lightIndexBufRes =
            gpuGraphImportBuffer(graph, res.lightIndexBuffer, GPU_RESOURCE_STATE_UNORDERED_ACCESS, "light_index_buffer");
    }

    if (features.dirShadows) {
        ShadowPassData shadowPassData[4] = {};
        for (int ci = 0; ci < 4; ci++) {
            shadowPassData[ci].frame = &fd;
            shadowPassData[ci].type = ShadowPassType::Cascade;
            shadowPassData[ci].cascadeIndex = ci;

            char pn[32];
            snprintf(pn, sizeof(pn), "shadow_cascade_%d", ci);
            GpuGraphPass sp = gpuGraphAddRenderPass(graph, pn);
            GpuGraphDepthAttachment sda = {};
            sda.resource = cascadeRes[ci];
            sda.loadOp = GPU_LOAD_OP_CLEAR;
            sda.storeOp = GPU_STORE_OP_STORE;
            sda.clearDepth = 1.0f;
            sda.depthViewOverride = res.cascadeDepthView[ci];
            gpuGraphPassSetDepthAttachment(sp, &sda);
            gpuGraphPassSetCallback(sp, shadowPassCallback, &shadowPassData[ci]);
        }
    }

    if (features.pointShadows) {
        const uint32_t pointShadowSlots = effectivePointShadowSlotCount(fd);
        ShadowPassData pointFacePassData[kMaxPointShadowSlots * kCubeFaceCount] = {};
        uint32_t pointPassIndex = 0;
        for (uint32_t pi = 0; pi < pointShadowSlots; pi++) {
            for (uint32_t face = 0; face < kCubeFaceCount; face++) {
                pointFacePassData[pointPassIndex].frame = &fd;
                pointFacePassData[pointPassIndex].type = ShadowPassType::PointCubeFace;
                pointFacePassData[pointPassIndex].pointShadowSlot = (int)pi;
                pointFacePassData[pointPassIndex].cubeFace = (int)face;

                char pn[64];
                snprintf(pn, sizeof(pn), "point_shadow_%u_face_%u", pi, face);
                GpuGraphPass sp = gpuGraphAddRenderPass(graph, pn);
                GpuGraphDepthAttachment sda = {};
                sda.resource = pointShadowRes[pi];
                sda.loadOp = (face == 0) ? GPU_LOAD_OP_CLEAR : GPU_LOAD_OP_LOAD;
                sda.storeOp = GPU_STORE_OP_STORE;
                sda.clearDepth = 1.0f;
                sda.depthViewOverride = res.pointShadowCubeFaceDSV[pi][face];
                gpuGraphPassSetDepthAttachment(sp, &sda);
                gpuGraphPassSetCallback(sp, shadowPassCallback, &pointFacePassData[pointPassIndex]);
                pointPassIndex++;
            }
        }
    }

    if (features.gbuffer) {
        GpuGraphPass gp = gpuGraphAddRenderPass(graph, "gbuffer_pass");
        GpuGraphColorAttachment gbufColors[2] = {};
        gbufColors[0].resource = albedoRes;
        gbufColors[0].loadOp = GPU_LOAD_OP_CLEAR;
        gbufColors[0].storeOp = GPU_STORE_OP_STORE;
        gbufColors[0].clearColor[0] = 0;
        gbufColors[0].clearColor[1] = 0;
        gbufColors[0].clearColor[2] = 0;
        gbufColors[0].clearColor[3] = 1;
        gbufColors[1].resource = normalRes;
        gbufColors[1].loadOp = GPU_LOAD_OP_CLEAR;
        gbufColors[1].storeOp = GPU_STORE_OP_STORE;
        gbufColors[1].clearColor[0] = 0.5f;
        gbufColors[1].clearColor[1] = 0.5f;
        gbufColors[1].clearColor[2] = 1.0f;
        gbufColors[1].clearColor[3] = 1;
        gpuGraphPassSetColorAttachments(gp, 2, gbufColors);
        GpuGraphDepthAttachment gda = {};
        gda.resource = depthRes;
        gda.loadOp = GPU_LOAD_OP_CLEAR;
        gda.storeOp = GPU_STORE_OP_STORE;
        gda.clearDepth = 1.0f;
        gda.depthViewOverride = res.sceneDepthDsv;
        gpuGraphPassSetDepthAttachment(gp, &gda);
        gpuGraphPassSetCallback(gp, gbufferPassCallback, &fd);
    }

    if (features.lightCull) {
        GpuGraphPass lcp = gpuGraphAddComputePass(graph, "light_cull");
        gpuGraphPassRead(lcp, lightBufRes);
        gpuGraphPassRead(lcp, depthRes);
        gpuGraphPassReadWrite(lcp, lightIndexBufRes);
        gpuGraphPassSetCallback(lcp, lightCullPassCallback, &fd);
    }

    if (features.ssgi) {
        GpuGraphPass ssp = gpuGraphAddComputePass(graph, "ssgi");
        gpuGraphPassRead(ssp, depthRes);
        gpuGraphPassRead(ssp, albedoRes);
        gpuGraphPassRead(ssp, normalRes);
        gpuGraphPassReadWrite(ssp, ssgiRes);
        gpuGraphPassSetCallback(ssp, ssgiPassCallback, &fd);
    }

    GpuGraphPass fp = gpuGraphAddRenderPass(graph, "forward_pass");
    GpuGraphColorAttachment ca = {};
    ca.resource = bbRes;
    ca.loadOp = GPU_LOAD_OP_CLEAR;
    ca.storeOp = GPU_STORE_OP_STORE;
    ca.clearColor[0] = 0.025f;
    ca.clearColor[1] = 0.030f;
    ca.clearColor[2] = 0.040f;
    ca.clearColor[3] = 1.0f;
    gpuGraphPassSetColorAttachments(fp, 1, &ca);

    GpuGraphDepthAttachment da = {};
    da.resource = depthRes;
    da.loadOp = GPU_LOAD_OP_CLEAR;
    da.storeOp = GPU_STORE_OP_STORE;
    da.clearDepth = 1.0f;
    da.depthViewOverride = res.sceneDepthDsv;
    gpuGraphPassSetDepthAttachment(fp, &da);

    if (features.dirShadows) {
        for (int ci = 0; ci < 4; ci++) gpuGraphPassRead(fp, cascadeRes[ci]);
    }
    if (features.pointShadows) {
        const uint32_t pointShadowSlots = effectivePointShadowSlotCount(fd);
        for (uint32_t pi = 0; pi < pointShadowSlots; pi++) gpuGraphPassRead(fp, pointShadowRes[pi]);
    }
    if (features.pointLights) gpuGraphPassRead(fp, lightBufRes);
    gpuGraphPassSetCallback(fp, forwardPassCallback, &fd);

    bool ok = true;
    if (gpuGraphCompile(graph) != GPU_SUCCESS) {
        printf("Graph compile failed\n");
        ok = false;
    } else {
        if (fd.diagShadow) {
            uint32_t execCount = gpuGraphGetExecutionOrderCount(graph);
            printf("[diag] render graph execution order (%u passes):\n", execCount);
            for (uint32_t si = 0; si < execCount; si++) {
                uint32_t pi = gpuGraphGetExecutionOrderPassIndex(graph, si);
                const char* name = gpuGraphGetPassName(graph, pi);
                bool culled = gpuGraphIsPassCulled(graph, pi);
                printf("  [%u] pass=%u name=%s culled=%d\n", si, pi, name ? name : "?", culled ? 1 : 0);
            }
            gpuGraphExportJson(graph, "shadow_diag_graph.json");
        }

        GpuCommandQueue queue = nullptr;
        gpuGetQueue(ctx.device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
        if (gpuGraphExecute(graph, queue) != GPU_SUCCESS) {
            printf("Graph execute failed\n");
            ok = false;
        } else {
            gpuQueueWaitOnHost(queue);
        }
    }

    gpuGraphDestroy(graph);
    return ok;
}
