#include "placed_lights.h"

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
