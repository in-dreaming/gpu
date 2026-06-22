#pragma once

#include "core/types.h"
#include "gpu/gpu.h"
#include <vector>
#include <cstdint>

class LightSystem {
public:
    static constexpr uint32_t kMaxLights = 1024;
    static constexpr uint32_t kLightsPerTile = 32;

    bool init(GpuDevice device);
    void shutdown();

    void addLight(const Vec3& position, float radius, const Vec3& color);
    void clear();
    void animate(float time, Vec3 center);

    GpuBufferHandle getLightBuffer() const { return mLightBuffer; }
    uint32_t getLightCount() const { return mLightCount; }
    uint32_t getCapacity() const { return kMaxLights; }

private:
    GpuDevice mDevice = nullptr;
    GpuBufferHandle mLightBuffer = GPU_NULL_HANDLE;
    uint32_t mLightCount = 0;
    PointLightData mLights[kMaxLights];
};

class LightCullSystem {
public:
    bool init(GpuDevice device, uint32_t maxLights);
    void shutdown();
    bool createPipeline(GpuDevice device, const char* shaderPath);

    void cullLights(GpuCommandEncoder encoder, const LightCullParams& params,
                    GpuBufferHandle lightBuffer, GpuBufferHandle outIndexBuffer,
                    uint32_t lightCount, uint32_t screenWidth, uint32_t screenHeight);

private:
    GpuDevice mDevice = nullptr;
    GpuComputePipeline mPipeline = nullptr;
    GpuShaderProgram mProgram = nullptr;
    uint32_t mMaxLights = 0;
};
