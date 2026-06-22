#include "light_system.h"
#include <cstdio>
#include <cmath>

bool LightSystem::init(GpuDevice device) {
    mDevice = device;
    GpuBufferDesc bd = {};
    bd.size = kMaxLights * sizeof(PointLightData);
    bd.usage = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_COPY_DEST;
    bd.defaultState = GPU_RESOURCE_STATE_COPY_DEST;
    bd.label = "light_buffer";
    return gpuCreateBuffer(device, &bd, &mLightBuffer) == GPU_SUCCESS;
}

void LightSystem::shutdown() {
    if (mLightBuffer.index) {
        gpuDestroyBuffer(mDevice, mLightBuffer);
        mLightBuffer = GPU_NULL_HANDLE;
    }
}

void LightSystem::clear() { mLightCount = 0; }

void LightSystem::addLight(const Vec3& position, float radius, const Vec3& color) {
    if (mLightCount >= kMaxLights) return;
    PointLightData& l = mLights[mLightCount++];
    l.position[0] = position.x; l.position[1] = position.y; l.position[2] = position.z;
    l.radius = radius;
    l.color[0] = color.x; l.color[1] = color.y; l.color[2] = color.z;
}

void LightSystem::animate(float t, Vec3 center) {
    Vec3 lights[kMaxLights];
    float radii[kMaxLights];
    Vec3 colors[kMaxLights];

    // Generate animated light positions
    uint32_t n = mLightCount;
    for (uint32_t i = 0; i < n; i++) {
        float angle0 = (float)i / (float)n * 2.0f * 3.14159265f + t * 0.3f;
        float angle1 = (float)(i * 7) / (float)n * 2.0f * 3.14159265f + t * 0.5f;
        float radius = 200.0f + (float)(i % 5) * 150.0f;
        float heightVar = sinf(angle1 * 0.5f) * 150.0f;
        lights[i] = {
            center.x + cosf(angle0) * radius,
            center.y + 80.0f + heightVar,
            center.z + sinf(angle0) * radius
        };
        radii[i] = 120.0f + (float)(i % 3) * 80.0f;
        float hue = (float)i / (float)n;
        float r = 0.5f + 0.5f * sinf(hue * 3.14159f * 2.0f);
        float g = 0.5f + 0.5f * sinf((hue + 0.33f) * 3.14159f * 2.0f);
        float b = 0.5f + 0.5f * sinf((hue + 0.67f) * 3.14159f * 2.0f);
        colors[i] = {r, g, b};
    }

    MemBuffer staging;
    if (gpuCreateStagingBuffer(mDevice, n * sizeof(PointLightData), &staging) != GPU_SUCCESS) return;
    PointLightData* mapped = (PointLightData*)gpuMapStagingBuffer(staging);
    if (mapped) {
        for (uint32_t i = 0; i < n; i++) {
            mapped[i].position[0] = lights[i].x; mapped[i].position[1] = lights[i].y; mapped[i].position[2] = lights[i].z;
            mapped[i].radius = radii[i];
            mapped[i].color[0] = colors[i].x; mapped[i].color[1] = colors[i].y; mapped[i].color[2] = colors[i].z;
            // Also update cached copy
            mLights[i] = mapped[i];
        }
        gpuUnmapStagingBuffer(staging);
        GpuCommandEncoder encoder = gpuBeginCommandEncoder(mDevice, nullptr);
        gpuCmdCopyBufferToBuffer(encoder, staging.buffer, 0, mLightBuffer, 0, n * sizeof(PointLightData));
        GpuCommandBuffer cmd = gpuFinishCommandEncoder(encoder);
        if (cmd) {
            GpuCommandQueue queue = nullptr;
            gpuGetQueue(mDevice, GPU_QUEUE_TYPE_GRAPHICS, &queue);
            gpuQueueSubmit(queue, 1, &cmd);
            gpuQueueWaitOnHost(queue);
        }
        gpuDestroyStagingBuffer(mDevice, staging);
    }
}

bool LightCullSystem::init(GpuDevice device, uint32_t maxLights) {
    mDevice = device;
    mMaxLights = maxLights;
    return true;
}

void LightCullSystem::shutdown() {
    // TODO: cleanup if compute pipeline created
}
