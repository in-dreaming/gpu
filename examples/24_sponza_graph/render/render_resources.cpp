#include "render_resources.h"
#include "render/bindless_bind.h"
#include <cstdio>

static uint32_t bindlessAllocateOrFail(GpuBindlessHeap heap, GpuHandle handle)
{
    uint32_t index = gpuBindlessAllocate(heap, handle);
    if (index == UINT32_MAX) {
        printf("Bindless allocation failed for handle (%u, %u)\n", handle.index, handle.generation);
    }
    return index;
}

static uint32_t bindlessAllocateBufferOrFail(GpuBindlessHeap heap, GpuBufferHandle buffer, uint32_t access)
{
    uint32_t index = gpuBindlessAllocateBuffer(
        heap,
        GpuHandle{buffer.index, buffer.generation},
        access);
    if (index == UINT32_MAX) {
        printf("Bindless buffer allocation failed for handle (%u, %u) access=%u\n",
               buffer.index, buffer.generation, access);
    }
    return index;
}

static uint32_t bindlessAllocateViewOrFail(GpuBindlessHeap heap, GpuTextureHandle view, uint32_t access)
{
    uint32_t index = gpuBindlessAllocateTextureView(
        heap,
        GpuHandle{view.index, view.generation},
        access);
    if (index == UINT32_MAX) {
        printf("Bindless texture-view allocation failed for handle (%u, %u)\n", view.index, view.generation);
    }
    return index;
}

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

    // Point-shadow depth read sampler (non-comparison, for cube radial compare)
    {
        GpuSamplerDesc sd = {};
        sd.minFilter = GPU_FILTER_LINEAR;
        sd.magFilter = GPU_FILTER_LINEAR;
        sd.mipFilter = GPU_FILTER_NEAREST;
        sd.addressModeU = GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sd.addressModeV = GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sd.addressModeW = GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sd.compareEnable = false;
        sd.label = "point_shadow_depth_sampler";
        if (gpuCreateSampler(r.device, &sd, &r.pointShadowDepthSampler) != GPU_SUCCESS) return false;
    }

    // Scene G-buffer
    recreateGBuffer(r, w, h);

    // Light buffer (SSBO)
    {
        GpuBufferDesc bd = {};
        bd.size = (uint64_t)maxLights * sizeof(PointLightData);
        bd.elementSize = sizeof(PointLightData);
        bd.usage = GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_COPY_DEST;
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
        bd.elementSize = sizeof(uint32_t);
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

    // Point-light cube shadow maps (6 faces per slot)
    for (uint32_t i = 0; i < kMaxPointShadowSlots; i++) {
        GpuTextureDesc td = {};
        td.type = GPU_TEXTURE_TYPE_CUBE;
        td.width = kPointShadowMapSize; td.height = kPointShadowMapSize; td.depth = 1;
        td.arrayLength = 1; td.mipCount = 1;
        td.format = GPU_FORMAT_D32_FLOAT;
        td.sampleCount = 1;
        td.usage = GPU_TEXTURE_USAGE_DEPTH_STENCIL | GPU_TEXTURE_USAGE_SHADER_RESOURCE;
        char label[64]; snprintf(label, sizeof(label), "point_shadow_cube_%u", i); td.label = label;
        if (gpuCreateTexture(r.device, &td, &r.pointShadowCube[i]) != GPU_SUCCESS) return false;
        gpuCreateTextureView(r.device, r.pointShadowCube[i], GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &r.pointShadowCubeSRV[i]);
        for (uint32_t face = 0; face < kCubeFaceCount; face++) {
            GpuTextureSubresourceRange sr = {};
            sr.mip = 0;
            sr.mipCount = 1;
            sr.layer = face;
            sr.layerCount = 1;
            if (gpuCreateTextureSubresourceView(
                    r.device,
                    r.pointShadowCube[i],
                    GPU_TEXTURE_VIEW_TYPE_DEPTH_STENCIL,
                    &sr,
                    &r.pointShadowCubeFaceDSV[i][face]) != GPU_SUCCESS) {
                return false;
            }
        }
    }

    // Bindless heaps
    {
        GpuBindlessHeapDesc textureDesc = {};
        textureDesc.maxDescriptors = 96;
        textureDesc.descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE;
        if (gpuCreateBindlessHeap(r.device, &textureDesc, &r.textureBindlessHeap) != GPU_SUCCESS) {
            printf("Texture bindless heap creation failed\n");
            return false;
        }

        GpuBindlessHeapDesc samplerDesc = {};
        samplerDesc.maxDescriptors = 8;
        samplerDesc.descriptorType = GPU_DESCRIPTOR_TYPE_SAMPLER;
        if (gpuCreateBindlessHeap(r.device, &samplerDesc, &r.samplerBindlessHeap) != GPU_SUCCESS) {
            printf("Sampler bindless heap creation failed\n");
            return false;
        }

        GpuBindlessHeapDesc bufferDesc = {};
        bufferDesc.maxDescriptors = 16;
        bufferDesc.descriptorType = GPU_DESCRIPTOR_TYPE_BUFFER;
        if (gpuCreateBindlessHeap(r.device, &bufferDesc, &r.bufferBindlessHeap) != GPU_SUCCESS) {
            printf("Buffer bindless heap creation failed\n");
            return false;
        }
    }

    return true;
}

bool registerBindlessResources(RenderResources& r, const MaterialTextures& materials) {
    if (!r.textureBindlessHeap || !r.samplerBindlessHeap || !r.bufferBindlessHeap) return false;

    r.bindless = {};

    r.bindless.baseColorTexture = bindlessAllocateViewOrFail(
        r.textureBindlessHeap,
        materials.baseColorView,
        GPU_DESCRIPTOR_ACCESS_READ);
    if (r.bindless.baseColorTexture == UINT32_MAX) {
        printf("Bindless base-color allocation failed\n");
        return false;
    }

    r.bindless.linearSampler = bindlessAllocateOrFail(
        r.samplerBindlessHeap,
        GpuHandle{r.linearSampler.index, r.linearSampler.generation});
    if (r.bindless.linearSampler == UINT32_MAX) return false;

    r.bindless.shadowSampler = bindlessAllocateOrFail(
        r.samplerBindlessHeap,
        GpuHandle{r.shadowSampler.index, r.shadowSampler.generation});
    if (r.bindless.shadowSampler == UINT32_MAX) return false;

    r.bindless.lightBuffer = bindlessAllocateBufferOrFail(
        r.bufferBindlessHeap,
        r.lightBuffer,
        GPU_DESCRIPTOR_ACCESS_READ);
    if (r.bindless.lightBuffer == UINT32_MAX) return false;

    r.bindless.lightIndexBuffer = bindlessAllocateBufferOrFail(
        r.bufferBindlessHeap,
        r.lightIndexBuffer,
        GPU_DESCRIPTOR_ACCESS_READ_WRITE);
    if (r.bindless.lightIndexBuffer == UINT32_MAX) return false;

    for (int i = 0; i < 4; i++) {
        r.bindless.cascadeShadows[i] = bindlessAllocateViewOrFail(
            r.textureBindlessHeap,
            r.cascadeSRV[i],
            GPU_DESCRIPTOR_ACCESS_READ);
        if (r.bindless.cascadeShadows[i] == UINT32_MAX) {
            printf("Failed to allocate bindless slot for cascade %d\n", i);
            return false;
        }
    }

    r.bindless.ssgiTexture = bindlessAllocateViewOrFail(
        r.textureBindlessHeap,
        r.ssgiOutputSrv,
        GPU_DESCRIPTOR_ACCESS_READ);
    if (r.bindless.ssgiTexture == UINT32_MAX) return false;

    r.bindless.sceneDepth = bindlessAllocateViewOrFail(
        r.textureBindlessHeap,
        r.sceneDepthSrv,
        GPU_DESCRIPTOR_ACCESS_READ);
    if (r.bindless.sceneDepth == UINT32_MAX) return false;

    r.bindless.sceneAlbedo = bindlessAllocateViewOrFail(
        r.textureBindlessHeap,
        r.sceneAlbedoSrv,
        GPU_DESCRIPTOR_ACCESS_READ);
    if (r.bindless.sceneAlbedo == UINT32_MAX) return false;

    r.bindless.sceneNormal = bindlessAllocateViewOrFail(
        r.textureBindlessHeap,
        r.sceneNormalSrv,
        GPU_DESCRIPTOR_ACCESS_READ);
    if (r.bindless.sceneNormal == UINT32_MAX) return false;

    r.bindless.ssgiOutputUav = bindlessAllocateViewOrFail(
        r.textureBindlessHeap,
        r.ssgiOutputUav,
        GPU_DESCRIPTOR_ACCESS_READ_WRITE);
    if (r.bindless.ssgiOutputUav == UINT32_MAX) return false;

    printf("Bindless heap slots: baseColor=%u cascades=[%u,%u,%u,%u] sceneDepth=%u albedo=%u normal=%u ssgi=%u ssgiUav=%u\n",
           r.bindless.baseColorTexture,
           r.bindless.cascadeShadows[0],
           r.bindless.cascadeShadows[1],
           r.bindless.cascadeShadows[2],
           r.bindless.cascadeShadows[3],
           r.bindless.sceneDepth,
           r.bindless.sceneAlbedo,
           r.bindless.sceneNormal,
           r.bindless.ssgiTexture,
           r.bindless.ssgiOutputUav);
    return true;
}

bool refreshSceneDepthBindlessHandle(RenderResources& r)
{
    if (!r.textureBindlessHeap || r.bindless.sceneDepth == UINT32_MAX || !r.sceneDepthSrv.index) return false;
    return gpuBindlessUpdateTextureView(
               r.textureBindlessHeap,
               r.bindless.sceneDepth,
               GpuHandle{r.sceneDepthSrv.index, r.sceneDepthSrv.generation},
               GPU_DESCRIPTOR_ACCESS_READ) == GPU_SUCCESS;
}

bool refreshGBufferBindlessHandles(RenderResources& r)
{
    bool ok = refreshSceneDepthBindlessHandle(r);
    if (!r.textureBindlessHeap) return false;
    if (r.bindless.sceneAlbedo != UINT32_MAX && r.sceneAlbedoSrv.index) {
        ok = gpuBindlessUpdateTextureView(
                 r.textureBindlessHeap,
                 r.bindless.sceneAlbedo,
                 GpuHandle{r.sceneAlbedoSrv.index, r.sceneAlbedoSrv.generation},
                 GPU_DESCRIPTOR_ACCESS_READ) == GPU_SUCCESS && ok;
    }
    if (r.bindless.sceneNormal != UINT32_MAX && r.sceneNormalSrv.index) {
        ok = gpuBindlessUpdateTextureView(
                 r.textureBindlessHeap,
                 r.bindless.sceneNormal,
                 GpuHandle{r.sceneNormalSrv.index, r.sceneNormalSrv.generation},
                 GPU_DESCRIPTOR_ACCESS_READ) == GPU_SUCCESS && ok;
    }
    return ok;
}

bool validateBindlessBindings(RenderResources& r)
{
    auto checkHeapSlot = [&](const char* name, GpuBindlessHeap heap, uint32_t index) -> bool {
        if (index == UINT32_MAX) {
            printf("Bindless validation failed: %s index invalid\n", name);
            return false;
        }
        GpuDescriptorHandleInfo info = {};
        if (gpuBindlessGetDescriptorHandle(heap, index, &info) != GPU_SUCCESS) {
            printf("Bindless validation failed: %s descriptor lookup failed\n", name);
            return false;
        }
        if (!gpuBindlessIsAllocated(heap, index)) {
            printf("Bindless validation failed: %s slot not allocated\n", name);
            return false;
        }
        return true;
    };

    if (!checkHeapSlot("baseColor", r.textureBindlessHeap, r.bindless.baseColorTexture)) return false;
    if (!checkHeapSlot("linearSampler", r.samplerBindlessHeap, r.bindless.linearSampler)) return false;
    if (!checkHeapSlot("shadowSampler", r.samplerBindlessHeap, r.bindless.shadowSampler)) return false;
    if (!checkHeapSlot("lightBuffer", r.bufferBindlessHeap, r.bindless.lightBuffer)) return false;
    if (!checkHeapSlot("lightIndexBuffer", r.bufferBindlessHeap, r.bindless.lightIndexBuffer)) return false;

    for (int i = 0; i < 4; i++) {
        if (!checkHeapSlot("cascadeShadow", r.textureBindlessHeap, r.bindless.cascadeShadows[i])) return false;
    }

    if (!checkHeapSlot("sceneDepth", r.textureBindlessHeap, r.bindless.sceneDepth)) return false;
    if (!checkHeapSlot("sceneAlbedo", r.textureBindlessHeap, r.bindless.sceneAlbedo)) return false;
    if (!checkHeapSlot("sceneNormal", r.textureBindlessHeap, r.bindless.sceneNormal)) return false;
    if (!checkHeapSlot("ssgiTexture", r.textureBindlessHeap, r.bindless.ssgiTexture)) return false;
    if (!checkHeapSlot("ssgiOutputUav", r.textureBindlessHeap, r.bindless.ssgiOutputUav)) return false;

    {
        GpuDescriptorHandleInfo baseInfo = {};
        GpuDescriptorHandleInfo sampInfo = {};
        gpuBindlessGetDescriptorHandle(r.textureBindlessHeap, r.bindless.baseColorTexture, &baseInfo);
        gpuBindlessGetDescriptorHandle(r.samplerBindlessHeap, r.bindless.linearSampler, &sampInfo);
        printf("Bindless descriptor types: baseColor=%u linearSampler=%u\n", baseInfo.type, sampInfo.type);
    }

    printf("Bindless heap bindings validated (textures/buffers/samplers)\n");
    return true;
}

void recreateGBuffer(RenderResources& r, uint32_t w, uint32_t h) {
    r.surfaceWidth = w;
    r.surfaceHeight = h;

    if (r.sceneDepthSrv.index) { gpuDestroyTextureView(r.device, r.sceneDepthSrv); r.sceneDepthSrv = GPU_NULL_HANDLE; }
    if (r.sceneDepthDsv.index) { gpuDestroyTextureView(r.device, r.sceneDepthDsv); r.sceneDepthDsv = GPU_NULL_HANDLE; }
    if (r.sceneDepth.index) { gpuDestroyTexture(r.device, r.sceneDepth); r.sceneDepth = GPU_NULL_HANDLE; }
    if (r.sceneAlbedoSrv.index) { gpuDestroyTextureView(r.device, r.sceneAlbedoSrv); r.sceneAlbedoSrv = GPU_NULL_HANDLE; }
    if (r.sceneAlbedoRtv.index) { gpuDestroyTextureView(r.device, r.sceneAlbedoRtv); r.sceneAlbedoRtv = GPU_NULL_HANDLE; }
    if (r.sceneAlbedo.index) { gpuDestroyTexture(r.device, r.sceneAlbedo); r.sceneAlbedo = GPU_NULL_HANDLE; }
    if (r.sceneNormalSrv.index) { gpuDestroyTextureView(r.device, r.sceneNormalSrv); r.sceneNormalSrv = GPU_NULL_HANDLE; }
    if (r.sceneNormalRtv.index) { gpuDestroyTextureView(r.device, r.sceneNormalRtv); r.sceneNormalRtv = GPU_NULL_HANDLE; }
    if (r.sceneNormal.index) { gpuDestroyTexture(r.device, r.sceneNormal); r.sceneNormal = GPU_NULL_HANDLE; }

    {
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

    auto createColorGBuffer = [&](GpuTextureHandle& tex, GpuTextureHandle& rtv, GpuTextureHandle& srv, const char* label) {
        GpuTextureDesc td = {};
        td.type = GPU_TEXTURE_TYPE_2D;
        td.width = w; td.height = h; td.depth = 1;
        td.arrayLength = 1; td.mipCount = 1;
        td.format = GPU_FORMAT_RGBA16_FLOAT;
        td.sampleCount = 1;
        td.usage = GPU_TEXTURE_USAGE_RENDER_TARGET | GPU_TEXTURE_USAGE_SHADER_RESOURCE;
        td.label = label;
        if (gpuCreateTexture(r.device, &td, &tex) != GPU_SUCCESS) return;
        gpuCreateTextureView(r.device, tex, GPU_TEXTURE_VIEW_TYPE_RENDER_TARGET, &rtv);
        gpuCreateTextureView(r.device, tex, GPU_TEXTURE_VIEW_TYPE_SHADER_RESOURCE, &srv);
    };

    createColorGBuffer(r.sceneAlbedo, r.sceneAlbedoRtv, r.sceneAlbedoSrv, "scene_albedo");
    createColorGBuffer(r.sceneNormal, r.sceneNormalRtv, r.sceneNormalSrv, "scene_normal");
}

void recreateDepth(RenderResources& r, uint32_t w, uint32_t h) {
    recreateGBuffer(r, w, h);
}

void destroyRenderResources(RenderResources& r) {
    auto destroy = [&](auto& h, auto fn) { if (h.index) { fn(r.device, h); h = GPU_NULL_HANDLE; } };
    destroy(r.sceneDepthSrv, gpuDestroyTextureView);
    destroy(r.sceneDepthDsv, gpuDestroyTextureView);
    destroy(r.sceneDepth, gpuDestroyTexture);
    destroy(r.sceneAlbedoSrv, gpuDestroyTextureView);
    destroy(r.sceneAlbedoRtv, gpuDestroyTextureView);
    destroy(r.sceneAlbedo, gpuDestroyTexture);
    destroy(r.sceneNormalSrv, gpuDestroyTextureView);
    destroy(r.sceneNormalRtv, gpuDestroyTextureView);
    destroy(r.sceneNormal, gpuDestroyTexture);
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
    destroy(r.pointShadowDepthSampler, gpuDestroySampler);

    for (int i = 0; i < 4; i++) {
        destroy(r.cascadeSRV[i], gpuDestroyTextureView);
        destroy(r.cascadeDepthView[i], gpuDestroyTextureView);
        destroy(r.cascadeDepth[i], gpuDestroyTexture);
    }

    for (uint32_t i = 0; i < kMaxPointShadowSlots; i++) {
        for (uint32_t face = 0; face < kCubeFaceCount; face++)
            destroy(r.pointShadowCubeFaceDSV[i][face], gpuDestroyTextureView);
        destroy(r.pointShadowCubeSRV[i], gpuDestroyTextureView);
        destroy(r.pointShadowCube[i], gpuDestroyTexture);
    }

    if (r.textureBindlessHeap) { gpuDestroyBindlessHeap(r.textureBindlessHeap); r.textureBindlessHeap = nullptr; }
    if (r.samplerBindlessHeap) { gpuDestroyBindlessHeap(r.samplerBindlessHeap); r.samplerBindlessHeap = nullptr; }
    if (r.bufferBindlessHeap) { gpuDestroyBindlessHeap(r.bufferBindlessHeap); r.bufferBindlessHeap = nullptr; }
    r.bindless = {};
}
