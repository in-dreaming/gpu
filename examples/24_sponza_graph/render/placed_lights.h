#pragma once

#include "core/types.h"

struct CameraParams;

enum class PlacedLightKind : uint32_t {
    Omni = 0,
    Spot = 1,
};

struct PlacedPointLightSlot {
    bool active = false;
    PointLightData data = {};
    float forward[3] = {};
};

struct PlacedPointLights {
    static constexpr uint32_t kMaxSlots = 9;

    PlacedPointLightSlot slots[kMaxSlots] = {};
    PlacedLightKind placeKind = PlacedLightKind::Spot;

    void clear(uint32_t index);
    void clearAll();
    uint32_t requiredLightCount() const;
};
