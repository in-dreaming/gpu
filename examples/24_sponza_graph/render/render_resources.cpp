#include "render_resources.h"
#include <cstdio>

static constexpr uint32_t kShadowMapSize = 2048;

bool initRenderResources(RenderResources& r, uint32_t w, uint32_t h, uint32_t maxLights) {
    r.surfaceWidth = w;
    r.surfaceHeight = h;

    // Linear sampler
    {
        GpuSamplerDesc sd = {};
        sd.minFilter = GPU_FILTER_LINEAR;
        sd.magFilter = GPU_FILTER_LINEAR;
        sd.mipFilter = GPU_FILTER_LINEAR;
        sd.addressModeU = GPU_SAMPLER_ADDRESS_MODE_REPEAT;
        sd.addressModeV = GPU_SAMPLER_ADDRESS_MODE_REPEAT;
        sd.addressModeW = GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sd.label = "linear_sampler";
        if (gpuCreateSampler(r.device, &sd, &r.linearSampler) != GPU_SUCCESS) return false;
    }

    // Shadow comparison sampler
    {
        GpuSamplerDesc sd = {};
        sd.minFilter = GPU_FILTER_LINEAR;
        sd.magFilter = GPU_FILTER_LINEAR;
        sd.mipFilter = GPU_FILTER_NEAREST;
        sd.addressModeU = GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sd.addressModeV = GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sd.addressModeW = GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sd.compareEnable = true;
        sd.compareOp = GPU_COMPARE_OP_LESS;
        sd.label = "shadow_sampler";
        if (gpuCreateSampler(r.device, &sd, &r.shadowSampler) != GPU_SUCCESS) return false;
    }

    // Scene depth
    recreateDepth(r, w, h);

    // Light buffer (SSBO)
    {
        GpuBufferDesc bd = {};
        bd.size = (uint64_t)maxLights * sizeof(PointLightData);
        bd.usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_COPY_DEST;
        bd.label = "light_buffer";
        if (gpuCreateBuffer(r.device, &bd, &r.lightBuffer) != GPU_SUCCESS) return false;
    }

    // Camera buffer
    {
        GpuBufferDesc bd = {};
        bd.size = sizeof(CameraParams);
        bd.usage = GPU_BUFFER_USAGE_CONSTANT_BUFFER | GPU_BUFFER_USAGE_COPY_DEST;
        bd.label = "camera_buffer";
        if (gpuCreateBuffer(r.device, &bd, &r.cameraBuffer) != GPU_SUCCESS) return false;
    }

    // Light index buffer (culling output)
    {
        uint32_t tileCount = ((w + 15) / 16) * ((h + 15) / 16);
        GpuBufferDesc bd = {};
        bd.size = (uint64_t)tileCount * 128 * sizeof(uint32_t); // up to 128 light indices per tile
        bd.usage = GPU_BUFFER_USAGE_UNORDERED_ACCESS | GPU_BUFFER_USAGE_SHADER_RESOURCE;
        bd.label = "light_index_buffer";
        if (gpuCreateBuffer(r.device, &bd, &r.lightIndexBuffer) != GPU_SUCCESS) return false;
    }

    // SSGI output (half-res)
    {
        GpuTextureDesc td = {};
        td.type = GPU_TEXTURE_TYPE_2D;
        td.width = w / 2; td.height = h / 2; td.depth = 1;
        td.arrayLength = 1; td.mipCount = 1;
        td.format = GPU_FORMAT_RGBA16_FLOAT;
        td.usage = GPU_TEXTURE_USAGE_UNORDERED_ACCESS | GPU_TEXTURE_USAGE_SHADER_RESOURCE;
        td.label = "ssgi_output";
        if (gpuCreateTexture(r.device, &td, &r.ssgiOutput) != GPU_SUCCESS) return false;
        gpuCreateTextureView(r.device, r.ssgiOutput, GPU_TEXTURE_VIEW_TYPE_UNORDERED_ACCESS, &r.ssgiOutputUav);
        gpuCreateTextureView(r.device, r.ssgiOutput, GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &r.ssgiOutputSrv);
    }

    // SSGI prev frame (for temporal)
    {
        GpuTextureDesc td = {};
        td.type = GPU_TEXTURE_TYPE_2D;
        td.width = w / 2; td.height = h / 2; td.depth = 1;
        td.arrayLength = 1; td.mipCount = 1;
        td.format = GPU_FORMAT_RGBA16_FLOAT;
        td.usage = GPU_TEXTURE_USAGE_UNORDERED_ACCESS | GPU_TEXTURE_USAGE_SHADER_RESOURCE;
        td.label = "ssgi_prev";
        if (gpuCreateTexture(r.device, &td, &r.ssgiPrev) != GPU_SUCCESS) return false;
        gpuCreateTextureView(r.device, r.ssgiPrev, GPU_TEXTURE_VIEW_TYPE_UNORDERED_ACCESS, &r.ssgiPrevView);
    }

    // CSM cascades
    for (int i = 0; i < 4; i++) {
        GpuTextureDesc td = {};
        td.type = GPU_TEXTURE_TYPE_2D;
        td.width = kShadowMapSize; td.height = kShadowMapSize; td.depth = 1;
        td.arrayLength = 1; td.mipCount = 1;
        td.format = GPU_FORMAT_D32_FLOAT;
        td.sampleCount = 1;
        td.usage = GPU_TEXTURE_USAGE_DEPTH_STENCIL | GPU_TEXTURE_USAGE_SHADER_RESOURCE;
        char label[64]; snprintf(label, sizeof(label), "cascade_%d", i); td.label = label;
        if (gpuCreateTexture(r.device, &td, &r.cascadeDepth[i]) != GPU_SUCCESS) return false;
        gpuCreateTextureView(r.device, r.cascadeDepth[i], GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL, &r.cascadeDepthView[i]);
        gpuCreateTextureView(r.device, r.cascadeDepth[i], GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &r.cascadeSRV[i]);
    }

    return true;
}

void recreateDepth(RenderResources& r, uint32_t w, uint32_t h) {
    r.surfaceWidth = w;
    r.surfaceHeight = h;
    if (r.sceneDepth.index) {
        if (r.sceneDepthSrv.index) gpuDestroyTextureView(r.device, r.sceneDepthSrv);
        if (r.sceneDepthDsv.index) gpuDestroyTextureView(r.device, r.sceneDepthDsv);
        gpuDestroyTexture(r.device, r.sceneDepth);
        r.sceneDepth = GPU_NULL_HANDLE;
        r.sceneDepthDsv = GPU_NULL_HANDLE;
        r.sceneDepthSrv = GPU_NULL_HANDLE;
    }
    GpuTextureDesc td = {};
    td.type = GPU_TEXTURE_TYPE_2D;
    td.width = w; td.height = h; td.depth = 1;
    td.arrayLength = 1; td.mipCount = 1;
    td.format = GPU_FORMAT_D32_FLOAT;
    td.sampleCount = 1;
    td.usage = GPU_TEXTURE_USAGE_DEPTH_STENCIL | GPU_TEXTURE_USAGE_SHADER_RESOURCE;
    td.label = "scene_depth";
    if (gpuCreateTexture(r.device, &td, &r.sceneDepth) == GPU_SUCCESS) {
        gpuCreateTextureView(r.device, r.sceneDepth, GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL, &r.sceneDepthDsv);
        gpuCreateTextureView(r.device, r.sceneDepth, GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &r.sceneDepthSrv);
    }
}

void destroyRenderResources(RenderResources& r) {
    auto destroy = [&](auto& h, auto fn) { if (h.index) { fn(r.device, h); h = GPU_NULL_HANDLE; } };
    destroy(r.sceneDepthSrv, gpuDestroyTextureView);
    destroy(r.sceneDepthDsv, gpuDestroyTextureView);
    destroy(r.sceneDepth, gpuDestroyTexture);
    destroy(r.vertexBuffer, gpuDestroyBuffer);
    destroy(r.indexBuffer, gpuDestroyBuffer);
    destroy(r.lightBuffer, gpuDestroyBuffer);
    destroy(r.cameraBuffer, gpuDestroyBuffer);
    destroy(r.lightIndexBuffer, gpuDestroyBuffer);

    destroy(r.ssgiOutputUav, gpuDestroyTextureView);
    destroy(r.ssgiOutputSrv, gpuDestroyTextureView);
    destroy(r.ssgiOutput, gpuDestroyTexture);
    destroy(r.ssgiPrevView, gpuDestroyTextureView);
    destroy(r.ssgiPrev, gpuDestroyTexture);

    destroy(r.linearSampler, gpuDestroySampler);
    destroy(r.shadowSampler, gpuDestroySampler);

    for (int i = 0; i < 4; i++) {
        destroy(r.cascadeSRV[i], gpuDestroyTextureView);
        destroy(r.cascadeDepthView[i], gpuDestroyTextureView);
        destroy(r.cascadeDepth[i], gpuDestroyTexture);
    }
}
