#include "camera.h"
#include <algorithm>
#include <cmath>

static float clampLocal(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

void updateCamera(FlyCamera& cam, const bool keys[256], float dt) {
    const float ms = 430, ls = 1.9f;
    if (keys['j']) cam.yaw -= ls * dt;
    if (keys['l']) cam.yaw += ls * dt;
    if (keys['i']) cam.pitch += ls * dt;
    if (keys['k']) cam.pitch -= ls * dt;
    cam.pitch = clampLocal(cam.pitch, -1.35f, 1.35f);
    float sy = sinf(cam.yaw), cy = cosf(cam.yaw);
    Vec3 fwd = {sy, 0, cy}, rgt = {cy, 0, -sy};
    Vec3 d = {};
    if (keys['w']) { d.x += fwd.x; d.z += fwd.z; }
    if (keys['s']) { d.x -= fwd.x; d.z -= fwd.z; }
    if (keys['d']) { d.x += rgt.x; d.z += rgt.z; }
    if (keys['a']) { d.x -= rgt.x; d.z -= rgt.z; }
    if (keys['e']) d.y += 1;
    if (keys['q']) d.y -= 1;
    cam.position.x += d.x * ms * dt;
    cam.position.y += d.y * ms * dt;
    cam.position.z += d.z * ms * dt;
}

void rotateCameraByMouse(FlyCamera& cam, int32_t dx, int32_t dy) {
    cam.yaw += dx * 0.0045f;
    cam.pitch -= dy * 0.0045f;
    cam.pitch = clampLocal(cam.pitch, -1.35f, 1.35f);
}

CameraParams makeCameraParams(const FlyCamera& cam, float aspect, float fov, float nearZ, float farZ) {
    float sy = sinf(cam.yaw), cy = cosf(cam.yaw), sp = sinf(cam.pitch), cp = cosf(cam.pitch);
    Vec3 fwd = normalize({sy * cp, sp, cy * cp});
    Vec3 rgt = normalize({cy, 0, -sy});
    Vec3 up = normalize(cross(fwd, rgt));
    float f = 1.0f / tanf(fov * 0.5f * kPi / 180.0f);
    CameraParams p = {};
    p.cameraPos[0] = cam.position.x; p.cameraPos[1] = cam.position.y; p.cameraPos[2] = cam.position.z;
    p.right[0] = rgt.x; p.right[1] = rgt.y; p.right[2] = rgt.z;
    p.up[0] = up.x; p.up[1] = up.y; p.up[2] = up.z;
    p.forward[0] = fwd.x; p.forward[1] = fwd.y; p.forward[2] = fwd.z;
    p.proj[0] = f / std::max(aspect, 0.01f); p.proj[1] = f;
    p.zParams[0] = nearZ; p.zParams[1] = farZ;
    return p;
}

CameraParams makeCameraLookAt(Vec3 pos, Vec3 target, float fov, float aspect, float nz, float fz) {
    Vec3 fwd = normalize(sub(target, pos));
    Vec3 rgt = normalize(cross(fwd, {0, 1, 0}));
    Vec3 up = normalize(cross(fwd, rgt));
    float f = 1.0f / tanf(fov * 0.5f * kPi / 180.0f);
    CameraParams p = {};
    p.cameraPos[0] = pos.x; p.cameraPos[1] = pos.y; p.cameraPos[2] = pos.z;
    p.right[0] = rgt.x; p.right[1] = rgt.y; p.right[2] = rgt.z;
    p.up[0] = up.x; p.up[1] = up.y; p.up[2] = up.z;
    p.forward[0] = fwd.x; p.forward[1] = fwd.y; p.forward[2] = fwd.z;
    p.proj[0] = f / std::max(aspect, 0.01f); p.proj[1] = f;
    p.zParams[0] = nz; p.zParams[1] = fz;
    return p;
}
