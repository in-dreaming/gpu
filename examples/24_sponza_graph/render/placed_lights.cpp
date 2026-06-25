#include "placed_lights.h"
#include "core/camera.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

float placedLightDefaultRadius(const Vec3& lightPos, const Vec3& boundsMin, const Vec3& boundsMax)
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

    float maxDist = 4.0f;
    for (int i = 0; i < 8; i++) {
        const float dx = corners[i][0] - lightPos.x;
        const float dy = corners[i][1] - lightPos.y;
        const float dz = corners[i][2] - lightPos.z;
        maxDist = std::max(maxDist, sqrtf(dx * dx + dy * dy + dz * dz));
    }
    return maxDist < 1.0f ? 1.0f : maxDist;
}

static void defaultPlacedColor(uint32_t index, float colorOut[3])
{
    static const float kPalette[9][3] = {
        {2.6f, 0.0f, 0.0f},
        {0.00f, 2.3f, 0.00f},
        {0.00f, 0.0f, 2.6f},
        {1.00f, 1.0f, 1.0f},
    };
    const uint32_t pi = index % 9u;
    colorOut[0] = kPalette[pi][0];
    colorOut[1] = kPalette[pi][1];
    colorOut[2] = kPalette[pi][2];
}

static void setPlacedSpotParams(PointLightData& light, const float forward[3])
{
    light.spotEnable = 1u;
    light.direction[0] = forward[0];
    light.direction[1] = forward[1];
    light.direction[2] = forward[2];
    constexpr float kDeg = 3.14159265f / 180.0f;
    light.innerConeCos = cosf(28.0f * kDeg);
    light.outerConeCos = cosf(42.0f * kDeg);
}

void PlacedPointLights::placeAtCamera(uint32_t index, const CameraParams& camera, const Vec3& boundsMin,
                                      const Vec3& boundsMax)
{
    if (index >= kMaxSlots) return;

    PlacedPointLightSlot& slot = slots[index];
    slot.active = true;

    slot.data.position[0] = camera.cameraPos[0];
    slot.data.position[1] = camera.cameraPos[1];
    slot.data.position[2] = camera.cameraPos[2];
    const Vec3 lightPos = {slot.data.position[0], slot.data.position[1], slot.data.position[2]};
    slot.data.radius = placedLightDefaultRadius(lightPos, boundsMin, boundsMax);
    slot.data.shadowMapIndex = -1;
    defaultPlacedColor(index, slot.data.color);

    float fx = camera.forward[0];
    float fy = camera.forward[1];
    float fz = camera.forward[2];
    const float len = sqrtf(fx * fx + fy * fy + fz * fz);
    if (len > 1e-5f) {
        slot.forward[0] = fx / len;
        slot.forward[1] = fy / len;
        slot.forward[2] = fz / len;
    } else {
        slot.forward[0] = 0.0f;
        slot.forward[1] = 0.0f;
        slot.forward[2] = 1.0f;
    }
    setPlacedSpotParams(slot.data, slot.forward);

    printf("Placed spotlight %u at (%.2f, %.2f, %.2f) radius=%.2f dir=(%.2f, %.2f, %.2f), color=(%.2f, %.2f, %.2f)\n",
           index + 1, slot.data.position[0], slot.data.position[1], slot.data.position[2], slot.data.radius,
           slot.data.direction[0], slot.data.direction[1], slot.data.direction[2], slot.data.color[0],
           slot.data.color[1], slot.data.color[2]);
}

void PlacedPointLights::clear(uint32_t index)
{
    if (index >= kMaxSlots) return;
    slots[index].active = false;
    slots[index].data = {};
    slots[index].forward[0] = slots[index].forward[1] = slots[index].forward[2] = 0.0f;
}

void PlacedPointLights::clearAll()
{
    for (uint32_t i = 0; i < kMaxSlots; i++) clear(i);
}

uint32_t PlacedPointLights::requiredLightCount() const
{
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxSlots; i++) {
        if (slots[i].active) count = i + 1;
    }
    return count;
}
