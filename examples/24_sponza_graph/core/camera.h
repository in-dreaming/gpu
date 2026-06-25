#pragma once

#include "core/types.h"
#include <cstdint>

struct FlyCamera {
    Vec3 position = {0, 120, -450};
    float yaw = 0, pitch = 0;
};

static constexpr float kPi = 3.14159265f;

CameraParams makeCameraParams(const FlyCamera& cam, float aspect, float fov = 60.0f, float nearZ = 3.0f, float farZ = 6000.0f);
CameraParams makeCameraLookAt(Vec3 pos, Vec3 target, float fov, float aspect, float nz, float fz);
void updateCamera(FlyCamera& cam, const bool keys[256], float dt, float moveSpeed = 430.0f, float lookSpeed = 1.9f);
void rotateCameraByMouse(FlyCamera& cam, int32_t dx, int32_t dy);
