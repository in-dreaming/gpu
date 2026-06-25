#pragma once

#include <cstdint>

// Directional light orientation in yaw/pitch (radians).
// dirLightDir is the direction sunlight travels (sun -> scene), same as shadow / forward.
struct DirLightController {
    float yaw = 0.0f;
    float pitch = 1.12f;
};

void dirLightSetFromTravelDir(DirLightController& ctrl, float dx, float dy, float dz);
void dirLightWriteTravelDir(const DirLightController& ctrl, float outDir[3]);
void updateDirLightController(DirLightController& ctrl, const bool keys[256], float dt);
