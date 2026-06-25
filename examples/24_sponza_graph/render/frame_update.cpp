#include "render/frame_data.h"
#include "render/placed_lights.h"
#include "core/simple_scene.h"

#include <algorithm>
#include <cmath>
#include <vector>

static void fillLightTestPointLights(PointLightData* lights, uint32_t count, const SimpleLightTestLayout& layout)
{
    const float cx = layout.clusterCenter.x;
    const float cz = layout.clusterCenter.z;
    const float y = layout.lightHeight;
    const float r = layout.lightRadius;

    static const float kColors[2][3] = {
        {12.0f, 2.2f, 1.4f},
        {1.4f, 2.4f, 12.0f},
    };

    for (uint32_t i = 0; i < count; i++) {
        lights[i].shadowMapIndex = -1;
        lights[i].radius = r;
        if (count == 1) {
            lights[i].position[0] = cx + layout.lightPairSeparation * 0.35f;
            lights[i].position[1] = y;
            lights[i].position[2] = cz + layout.lightPairSeparation * 0.2f;
        } else {
            const float side = layout.lightPairSeparation * 0.5f;
            lights[i].position[0] = cx + ((i == 0) ? -side : side);
            lights[i].position[1] = y;
            lights[i].position[2] = cz;
        }
        lights[i].color[0] = kColors[i % 2][0];
        lights[i].color[1] = kColors[i % 2][1];
        lights[i].color[2] = kColors[i % 2][2];
    }
}

static void fillSimpleInteriorPointLights(PointLightData* lights, uint32_t count, const SimpleRoomLayout& room)
{
    const float ceilingY = room.halfY * 2.0f - kSimpleSceneWorldScale * 0.55f;
    const float reach = room.halfY * 2.25f;
    const float spreadX = room.halfX * 0.42f;

    static const float kColors[2][3] = {
        {2.4f, 0.48f, 0.32f},
        {0.32f, 0.58f, 2.4f},
    };

    for (uint32_t i = 0; i < count; i++) {
        lights[i].shadowMapIndex = -1;
        lights[i].radius = reach;
        if (count == 1) {
            lights[i].position[0] = 0.0f;
            lights[i].position[1] = ceilingY;
            lights[i].position[2] = room.halfZ * 0.1f;
        } else {
            lights[i].position[0] = (i == 0) ? -spreadX : spreadX;
            lights[i].position[1] = ceilingY;
            lights[i].position[2] = (i == 0) ? -room.halfZ * 0.12f : room.halfZ * 0.18f;
        }
        lights[i].color[0] = kColors[i % 2][0];
        lights[i].color[1] = kColors[i % 2][1];
        lights[i].color[2] = kColors[i % 2][2];
    }
}

static void applyPlacedPointLightOverrides(FrameData& frame, PointLightData* lights, uint32_t count,
                                           uint32_t slotOffset = 0)
{
    for (uint32_t pi = 0; pi < PlacedPointLights::kMaxSlots; pi++) {
        if (!frame.placedPointLights.slots[pi].active) continue;
        const uint32_t li = slotOffset + pi;
        if (li >= count) break;
        lights[li] = frame.placedPointLights.slots[pi].data;
    }
}

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

static void assignPointShadowSlots(FrameData& frame, PointLightData* lights, uint32_t count, const Vec3& boundsMin,
                                   const Vec3& boundsMax)
{
    frame.pointShadowSlotCount = 0;
    if (!frame.features.pointShadows) return;

    const bool simpleRoom = frame.lightTestMode || frame.simpleSceneMode;
    const uint32_t shadowSlots = std::min(count, kMaxPointShadowSlots);
    frame.pointShadowSlotCount = shadowSlots;

    for (uint32_t i = 0; i < count; i++)
        lights[i].shadowMapIndex = -1;

    // Stable slot assignment by light index (never re-order by camera distance).
    for (uint32_t slot = 0; slot < shadowSlots; slot++) {
        const uint32_t li = slot;
        lights[li].shadowMapIndex = (int32_t)slot;

        Vec3 lpos = {lights[li].position[0], lights[li].position[1], lights[li].position[2]};
        frame.pointShadowLightPos[slot][0] = lpos.x;
        frame.pointShadowLightPos[slot][1] = lpos.y;
        frame.pointShadowLightPos[slot][2] = lpos.z;
        const float nearZ = simpleRoom ? kSimpleSceneWorldScale * 0.08f : 0.25f * kSimpleSceneWorldScale;
        const float farZ = pointShadowFarFromBounds(lpos, lights[li].radius, boundsMin, boundsMax);
        frame.pointShadowNear[slot] = nearZ;
        frame.pointShadowFar[slot] = std::max(farZ, nearZ + kSimpleSceneWorldScale * 0.5f);
        for (int face = 0; face < kCubeFaceCount; face++)
            buildPointCubeFaceViewProj(lpos, face, nearZ, frame.pointShadowFar[slot], frame.pointShadowViewProj[slot][face]);
    }

    if (shadowSlots < count) {
        const float fillScale = frame.lightTestMode ? 0.01f : 0.15f;
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
    if (!frame.features.pointLights || !frame.device || !frame.resources) return;

    const uint32_t count = frame.features.pointLightCount;

    if (frame.lightTestMode) {
        const SimpleLightTestLayout layout = getSimpleLightTestLayout();
        const uint32_t baseLights = std::max(1u, frame.lightTestPointCount);
        const uint32_t placedCount = frame.placedPointLights.requiredLightCount();
        const uint32_t uploadCount = std::max({count, baseLights + placedCount});

        std::vector<PointLightData> lights(uploadCount);
        fillLightTestPointLights(lights.data(), baseLights, layout);
        for (uint32_t i = baseLights; i < uploadCount; i++) {
            lights[i].radius = 0.0f;
            lights[i].color[0] = lights[i].color[1] = lights[i].color[2] = 0.0f;
            lights[i].shadowMapIndex = -1;
        }

        // Keys 1-9 add extra lights after the fixed ceiling pair; they do not replace them.
        applyPlacedPointLightOverrides(frame, lights.data(), uploadCount, baseLights);
        assignPointShadowSlots(frame, lights.data(), uploadCount, boundsMin, boundsMax);

        gpuUploadToBuffer(frame.device, frame.resources->lightBuffer, lights.data(), uploadCount * sizeof(PointLightData),
                          0);
        frame.lightCount = uploadCount;
        return;
    }

    std::vector<PointLightData> lights(count);

    if (frame.simpleSceneMode) {
        const SimpleRoomLayout room = getSimpleRoomLayout();
        const uint32_t active = std::min(count, 4u);
        fillSimpleInteriorPointLights(lights.data(), active, room);
        for (uint32_t i = active; i < count; i++) {
            lights[i].radius = 0.0f;
            lights[i].color[0] = lights[i].color[1] = lights[i].color[2] = 0.0f;
            lights[i].shadowMapIndex = -1;
        }

        applyPlacedPointLightOverrides(frame, lights.data(), count);
        assignPointShadowSlots(frame, lights.data(), count, boundsMin, boundsMax);

        gpuUploadToBuffer(frame.device, frame.resources->lightBuffer, lights.data(), count * sizeof(PointLightData), 0);
        frame.lightCount = count;
        return;
    }

    const Vec3 sceneCenter = {
        (boundsMin.x + boundsMax.x) * 0.5f,
        (boundsMin.y + boundsMax.y) * 0.5f,
        (boundsMin.z + boundsMax.z) * 0.5f,
    };
    const float extentX = boundsMax.x - boundsMin.x;
    const float extentY = boundsMax.y - boundsMin.y;
    const float extentZ = boundsMax.z - boundsMin.z;
    const float sceneExtent = std::max(std::max(extentX, extentY), std::max(extentZ, 4.0f));
    const float orbitRadius = std::max(sceneExtent * 1.25f, 6.0f);
    const float heightBase = sceneCenter.y + sceneExtent * 0.55f;
    const float heightVarAmp = sceneExtent * 0.22f;
    const float lightRadius = std::max(sceneExtent * 0.95f, 4.5f);

    for (uint32_t i = 0; i < count; i++) {
        float angle = (float)i / (float)count * 6.28318f + timeSec * 0.2f;
        float angle2 = (float)(i * 7 % 773) / 773.0f * 6.28318f + timeSec * 0.3f;
        float ring = orbitRadius * (0.85f + 0.15f * (float)(i % 3) / 2.0f);
        float heightVar = sinf(angle2) * heightVarAmp;
        lights[i].position[0] = sceneCenter.x + cosf(angle) * ring;
        lights[i].position[1] = heightBase + heightVar;
        lights[i].position[2] = sceneCenter.z + sinf(angle) * ring;
        lights[i].radius = lightRadius * (0.9f + 0.1f * (float)(i % 4));
        float hue = (float)i / (float)count;
        lights[i].color[0] = 0.55f + 0.45f * sinf(hue * 6.28318f + timeSec * 0.5f);
        lights[i].color[1] = 0.55f + 0.45f * sinf((hue + 0.33f) * 6.28318f + timeSec * 0.5f);
        lights[i].color[2] = 0.55f + 0.45f * sinf((hue + 0.67f) * 6.28318f + timeSec * 0.5f);
        if (sceneExtent < 25.0f) {
            lights[i].color[0] *= 2.5f;
            lights[i].color[1] *= 2.5f;
            lights[i].color[2] *= 2.5f;
        } else if (frame.features.pointShadows) {
            lights[i].color[0] *= 1.8f;
            lights[i].color[1] *= 1.8f;
            lights[i].color[2] *= 1.8f;
        }
        lights[i].shadowMapIndex = -1;
    }

    applyPlacedPointLightOverrides(frame, lights.data(), count);
    assignPointShadowSlots(frame, lights.data(), count, boundsMin, boundsMax);

    gpuUploadToBuffer(frame.device, frame.resources->lightBuffer, lights.data(), count * sizeof(PointLightData), 0);
    frame.lightCount = count;
}
