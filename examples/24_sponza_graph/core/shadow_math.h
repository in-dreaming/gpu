#pragma once

#include "core/camera.h"
#include "core/types.h"
#include <cstdint>

static constexpr int kCascadeCount = 4;

struct CascadeShadowData {
    float viewProj[16];
    float splitFar;
    float texelSize;
    float worldTexelSize;
    float depthBiasNdc;
};

void mat4Multiply(const float* a, const float* b, float* out);
void mat4TransformPoint(const float* m, const Vec3& p, Vec3& out);

void buildCameraFrustumCorners(const CameraParams& cam, float nearZ, float farZ, Vec3 corners[8]);
void buildCascadeSplitDistances(float nearZ, float farZ, int cascadeCount, float splitsOut[5]);

void computeOrthographicCascades(
    const CameraParams& camera,
    const Vec3& boundsMin,
    const Vec3& boundsMax,
    const float lightTravelDir[3],
    int cascadeCount,
    uint32_t shadowMapSize,
    CascadeShadowData cascadesOut[4]);

void buildPointCubeFaceViewProj(
    const Vec3& lightPos,
    int faceIndex,
    float nearZ,
    float farZ,
    float outViewProj[16]);

void cubeFaceTarget(const Vec3& lightPos, int faceIndex, Vec3& outTarget);
