#include "render/frame_data.h"
#include "core/demo_scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

static float pointShadowFarFromBounds(const Vec3& lpos, float attenuationRadius, const Vec3& boundsMin,
                                      const Vec3& boundsMax)
{
    const float corners[8][3] = {
        {boundsMin.x, boundsMin.y, boundsMin.z},
        {boundsMax.x, boundsMin.y, boundsMin.z},
        {boundsMin.x, boundsMax.y, boundsMin.z},
        {boundsMax.x, boundsMax.y, boundsMin.z},
        {boundsMin.x, boundsMin.y, boundsMax.z},
        {boundsMax.x, boundsMin.y, boundsMax.z},
        {boundsMin.x, boundsMax.y, boundsMax.z},
        {boundsMax.x, boundsMax.y, boundsMax.z},
    };

    float maxDist = attenuationRadius * 1.05f;
    for (int i = 0; i < 8; i++) {
        float dx = corners[i][0] - lpos.x;
        float dy = corners[i][1] - lpos.y;
        float dz = corners[i][2] - lpos.z;
        maxDist = std::max(maxDist, sqrtf(dx * dx + dy * dy + dz * dz));
    }
    return maxDist * 1.04f;
}

static void assignPointShadowSlots(FrameData& frame, const DemoScene& scene, PointLightData* lights, uint32_t count)
{
    frame.pointShadowSlotCount = 0;
    if (!frame.features.pointShadows) return;

    const uint32_t shadowSlots = std::min(count, kMaxPointShadowSlots);
    frame.pointShadowSlotCount = shadowSlots;

    for (uint32_t i = 0; i < count; i++)
        lights[i].shadowMapIndex = -1;

    const Vec3& boundsMin = scene.space.boundsMin;
    const Vec3& boundsMax = scene.space.boundsMax;
    const float nearZ = scene.space.pointShadowNear;

    for (uint32_t slot = 0; slot < shadowSlots; slot++) {
        const uint32_t li = slot;
        lights[li].shadowMapIndex = (int32_t)slot;

        Vec3 lpos = {lights[li].position[0], lights[li].position[1], lights[li].position[2]};
        frame.pointShadowLightPos[slot][0] = lpos.x;
        frame.pointShadowLightPos[slot][1] = lpos.y;
        frame.pointShadowLightPos[slot][2] = lpos.z;
        const float farZ = pointShadowFarFromBounds(lpos, lights[li].radius, boundsMin, boundsMax);
        frame.pointShadowNear[slot] = nearZ;
        frame.pointShadowFar[slot] = std::max(farZ, nearZ + scene.space.extent * 0.02f);
        for (int face = 0; face < kCubeFaceCount; face++)
            buildPointCubeFaceViewProj(lpos, face, nearZ, frame.pointShadowFar[slot], frame.pointShadowViewProj[slot][face]);
    }

    if (shadowSlots < count) {
        const float fillScale = scene.isLightTest() ? 0.01f : 0.15f;
        for (uint32_t i = 0; i < count; i++) {
            if (lights[i].shadowMapIndex >= 0) continue;
            lights[i].color[0] *= fillScale;
            lights[i].color[1] *= fillScale;
            lights[i].color[2] *= fillScale;
        }
    }
}

void updatePointLights(FrameData& frame, const Vec3& boundsMin, const Vec3& boundsMax, float timeSec)
{
    (void)boundsMin;
    (void)boundsMax;
    if (!frame.features.pointLights || !frame.device || !frame.resources || !frame.demoScene) return;

    const DemoScene& scene = *frame.demoScene;
    const uint32_t uploadCount =
        scene.resolvePointLightUploadCount(frame.features.pointLightCount, frame.placedPointLights);

    std::vector<PointLightData> lights(uploadCount);
    scene.fillBuiltinPointLights(lights.data(), uploadCount, timeSec);
    scene.applyPlacedLights(frame.placedPointLights, lights.data(), uploadCount);
    assignPointShadowSlots(frame, scene, lights.data(), uploadCount);

    gpuUploadToBuffer(frame.device, frame.resources->lightBuffer, lights.data(), uploadCount * sizeof(PointLightData), 0);
    frame.lightCount = uploadCount;
}
