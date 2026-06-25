#include "dir_light_control.h"

#include <algorithm>
#include <cmath>

static float clampLocal(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }

void dirLightSetFromTravelDir(DirLightController& ctrl, float dx, float dy, float dz)
{
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-6f) {
        ctrl.yaw = 0.0f;
        ctrl.pitch = 1.12f;
        return;
    }
    float nx = dx / len;
    float ny = dy / len;
    float nz = dz / len;
    ctrl.pitch = asinf(clampLocal(-ny, -1.0f, 1.0f));
    float cp = cosf(ctrl.pitch);
    if (fabsf(cp) > 1e-5f)
        ctrl.yaw = atan2f(nx / cp, nz / cp);
    else
        ctrl.yaw = 0.0f;
}

void dirLightWriteTravelDir(const DirLightController& ctrl, float outDir[3])
{
    float cp = cosf(ctrl.pitch);
    float sp = sinf(ctrl.pitch);
    float sy = sinf(ctrl.yaw);
    float cy = cosf(ctrl.yaw);
    outDir[0] = sy * cp;
    outDir[1] = -sp;
    outDir[2] = cy * cp;

    float len = sqrtf(outDir[0] * outDir[0] + outDir[1] * outDir[1] + outDir[2] * outDir[2]);
    if (len > 1e-6f) {
        outDir[0] /= len;
        outDir[1] /= len;
        outDir[2] /= len;
    }
}

void updateDirLightController(DirLightController& ctrl, const bool keys[256], float dt)
{
    const float rs = 1.4f;
    if (keys['u'])
        ctrl.yaw -= rs * dt;
    if (keys['o'])
        ctrl.yaw += rs * dt;
    if (keys['y'])
        ctrl.pitch += rs * dt;
    if (keys['h'])
        ctrl.pitch -= rs * dt;
    // Keep sun above the horizon (avoid light from below the ground).
    ctrl.pitch = clampLocal(ctrl.pitch, 0.08f, 1.52f);
}
