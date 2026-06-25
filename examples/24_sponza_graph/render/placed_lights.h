#pragma once

#include "core/types.h"

struct CameraParams;

struct PlacedPointLightSlot {
    bool active = false;
    PointLightData data = {};
    float forward[3] = {};
};

struct PlacedPointLights {
    static constexpr uint32_t kMaxSlots = 9;

    PlacedPointLightSlot slots[kMaxSlots] = {};

    void placeAtCamera(uint32_t index, const CameraParams& camera, const Vec3& boundsMin, const Vec3& boundsMax);
    void clear(uint32_t index);
    void clearAll();
    uint32_t requiredLightCount() const;
};

float placedLightDefaultRadius(const Vec3& lightPos, const Vec3& boundsMin, const Vec3& boundsMax);
