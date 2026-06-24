#include "shadow_math.h"
#include <algorithm>
#include <cmath>
#include <cstring>

void mat4Multiply(const float* a, const float* b, float* out)
{
    float r[16] = {};
    for (int c = 0; c < 4; c++) {
        for (int rIdx = 0; rIdx < 4; rIdx++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + rIdx] * b[c * 4 + k];
            r[c * 4 + rIdx] = sum;
        }
    }
    memcpy(out, r, sizeof(r));
}

void mat4TransformPoint(const float* m, const Vec3& p, Vec3& out)
{
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    out = {x, y, z};
}

static void buildLightBasis(const Vec3& lightToScene, Vec3& right, Vec3& up, Vec3& forward)
{
    forward = normalize(lightToScene);
    Vec3 worldUp = {0.0f, 1.0f, 0.0f};
    if (std::fabs(dot(forward, worldUp)) > 0.95f)
        worldUp = {0.0f, 0.0f, 1.0f};
    right = normalize(cross(worldUp, forward));
    up = normalize(cross(forward, right));
}

static void buildLightViewMatrix(const Vec3& origin, const Vec3& right, const Vec3& up, const Vec3& forward, float* out)
{
    memset(out, 0, sizeof(float) * 16);
    out[0] = right.x; out[1] = right.y; out[2] = right.z;
    out[4] = up.x; out[5] = up.y; out[6] = up.z;
    out[8] = forward.x; out[9] = forward.y; out[10] = forward.z;
    out[12] = -dot(right, origin);
    out[13] = -dot(up, origin);
    out[14] = -dot(forward, origin);
    out[15] = 1.0f;
}

static void buildOrthographicProjection(float minX, float maxX, float minY, float maxY, float minZ, float maxZ, float* out)
{
    memset(out, 0, sizeof(float) * 16);
    out[0] = 2.0f / (maxX - minX);
    out[5] = 2.0f / (maxY - minY);
    out[10] = 1.0f / (maxZ - minZ);
    out[14] = -minZ / (maxZ - minZ);
    out[15] = 1.0f;
}

static void buildPerspectiveProjection(const CameraParams& cam, float* out)
{
    memset(out, 0, sizeof(float) * 16);
    float n = cam.zParams[0];
    out[0] = cam.proj[0];
    out[5] = cam.proj[1];
    out[10] = 1.0f;
    out[11] = 1.0f;
    out[14] = -n;
}

static void buildCameraViewMatrix(const CameraParams& cam, float* out)
{
    Vec3 right = {cam.right[0], cam.right[1], cam.right[2]};
    Vec3 up = {cam.up[0], cam.up[1], cam.up[2]};
    Vec3 forward = {cam.forward[0], cam.forward[1], cam.forward[2]};
    Vec3 pos = {cam.cameraPos[0], cam.cameraPos[1], cam.cameraPos[2]};
    buildLightViewMatrix(pos, right, up, forward, out);
}

static void cameraParamsToViewProj(const CameraParams& cam, float* outViewProj)
{
    float view[16], proj[16];
    buildCameraViewMatrix(cam, view);
    buildPerspectiveProjection(cam, proj);
    mat4Multiply(proj, view, outViewProj);
}

void buildCameraFrustumCorners(const CameraParams& cam, float nearZ, float farZ, Vec3 corners[8])
{
    Vec3 pos = {cam.cameraPos[0], cam.cameraPos[1], cam.cameraPos[2]};
    Vec3 right = {cam.right[0], cam.right[1], cam.right[2]};
    Vec3 up = {cam.up[0], cam.up[1], cam.up[2]};
    Vec3 forward = {cam.forward[0], cam.forward[1], cam.forward[2]};

    float halfWNear = nearZ / cam.proj[0];
    float halfHNear = nearZ / cam.proj[1];
    float halfWFar = farZ / cam.proj[0];
    float halfHFar = farZ / cam.proj[1];

    Vec3 nearCenter = add(pos, mul(forward, nearZ));
    Vec3 farCenter = add(pos, mul(forward, farZ));

    Vec3 nearOffsets[4] = {
        add(mul(right, halfWNear), mul(up, halfHNear)),
        add(mul(right, -halfWNear), mul(up, halfHNear)),
        add(mul(right, -halfWNear), mul(up, -halfHNear)),
        add(mul(right, halfWNear), mul(up, -halfHNear)),
    };
    Vec3 farOffsets[4] = {
        add(mul(right, halfWFar), mul(up, halfHFar)),
        add(mul(right, -halfWFar), mul(up, halfHFar)),
        add(mul(right, -halfWFar), mul(up, -halfHFar)),
        add(mul(right, halfWFar), mul(up, -halfHFar)),
    };

    for (int i = 0; i < 4; i++) {
        corners[i] = add(nearCenter, nearOffsets[i]);
        corners[i + 4] = add(farCenter, farOffsets[i]);
    }
}

void buildCascadeSplitDistances(float nearZ, float farZ, int cascadeCount, float splitsOut[5])
{
    const float lambda = 0.5f;
    splitsOut[0] = nearZ;
    for (int i = 1; i <= cascadeCount; i++) {
        float p = (float)i / (float)cascadeCount;
        float logSplit = nearZ * std::pow(farZ / nearZ, p);
        float uniSplit = nearZ + (farZ - nearZ) * p;
        splitsOut[i] = lambda * logSplit + (1.0f - lambda) * uniSplit;
    }
}

static void expandBoundsInLightSpace(
    const Vec3& origin,
    const Vec3& right,
    const Vec3& up,
    const Vec3& forward,
    const Vec3& boundsMin,
    const Vec3& boundsMax,
    float& minX, float& maxX,
    float& minY, float& maxY,
    float& minZ, float& maxZ)
{
    Vec3 corners[8] = {
        {boundsMin.x, boundsMin.y, boundsMin.z},
        {boundsMax.x, boundsMin.y, boundsMin.z},
        {boundsMin.x, boundsMax.y, boundsMin.z},
        {boundsMax.x, boundsMax.y, boundsMin.z},
        {boundsMin.x, boundsMin.y, boundsMax.z},
        {boundsMax.x, boundsMin.y, boundsMax.z},
        {boundsMin.x, boundsMax.y, boundsMax.z},
        {boundsMax.x, boundsMax.y, boundsMax.z},
    };

    for (int i = 0; i < 8; i++) {
        Vec3 d = sub(corners[i], origin);
        float lx = dot(d, right);
        float ly = dot(d, up);
        float lz = dot(d, forward);
        minX = std::min(minX, lx); maxX = std::max(maxX, lx);
        minY = std::min(minY, ly); maxY = std::max(maxY, ly);
        minZ = std::min(minZ, lz); maxZ = std::max(maxZ, lz);
    }
}

void computeOrthographicCascades(
    const CameraParams& camera,
    const Vec3& boundsMin,
    const Vec3& boundsMax,
    const float lightTravelDir[3],
    int cascadeCount,
    uint32_t shadowMapSize,
    CascadeShadowData cascadesOut[4])
{
    // lightTravelDir: direction sunlight travels (sun -> scene), same as dirLightDir in forward.
    Vec3 lightForward = normalize(Vec3{lightTravelDir[0], lightTravelDir[1], lightTravelDir[2]});

    Vec3 right, up, forward;
    buildLightBasis(lightForward, right, up, forward);

    float nearZ = camera.zParams[0];
    float farZ = camera.zParams[1];
    float splits[5] = {};
    buildCascadeSplitDistances(nearZ, farZ, cascadeCount, splits);

    for (int ci = 0; ci < cascadeCount; ci++) {
        Vec3 corners[8];
        buildCameraFrustumCorners(camera, splits[ci], splits[ci + 1], corners);

        Vec3 frustumCenter = {};
        for (int i = 0; i < 8; i++)
            frustumCenter = add(frustumCenter, corners[i]);
        frustumCenter = mul(frustumCenter, 1.0f / 8.0f);

        float minX = 1e9f, maxX = -1e9f;
        float minY = 1e9f, maxY = -1e9f;
        float minZ = 1e9f, maxZ = -1e9f;

        for (int i = 0; i < 8; i++) {
            Vec3 d = sub(corners[i], frustumCenter);
            float lx = dot(d, right);
            float ly = dot(d, up);
            float lz = dot(d, forward);
            minX = std::min(minX, lx); maxX = std::max(maxX, lx);
            minY = std::min(minY, ly); maxY = std::max(maxY, ly);
            minZ = std::min(minZ, lz); maxZ = std::max(maxZ, lz);
        }

        expandBoundsInLightSpace(frustumCenter, right, up, forward, boundsMin, boundsMax,
                                 minX, maxX, minY, maxY, minZ, maxZ);

        float extX = (maxX - minX) * 0.5f;
        float extY = (maxY - minY) * 0.5f;
        float maxExtent = std::max(extX, extY);
        minX = -maxExtent; maxX = maxExtent;
        minY = -maxExtent; maxY = maxExtent;
        minZ -= 150.0f;
        maxZ += 400.0f;

        const float padXY = maxExtent * 0.08f;
        minX -= padXY; maxX += padXY;
        minY -= padXY; maxY += padXY;

        float texelWorld = (maxX - minX) / (float)shadowMapSize;
        float centerX = (minX + maxX) * 0.5f;
        float centerY = (minY + maxY) * 0.5f;
        centerX = std::floor(centerX / texelWorld) * texelWorld;
        centerY = std::floor(centerY / texelWorld) * texelWorld;
        minX = centerX - maxExtent;
        maxX = centerX + maxExtent;
        minY = centerY - maxExtent;
        maxY = centerY + maxExtent;

        float view[16], proj[16], viewProj[16];
        buildLightViewMatrix(frustumCenter, right, up, forward, view);
        buildOrthographicProjection(minX, maxX, minY, maxY, minZ, maxZ, proj);
        mat4Multiply(proj, view, viewProj);

        memcpy(cascadesOut[ci].viewProj, viewProj, sizeof(viewProj));
        cascadesOut[ci].splitFar = splits[ci + 1];
        cascadesOut[ci].texelSize = 1.0f / (float)shadowMapSize;
    }
}

void cubeFaceTarget(const Vec3& lightPos, int faceIndex, Vec3& outTarget)
{
    static const Vec3 kDirs[6] = {
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
    };
    outTarget = add(lightPos, kDirs[faceIndex % 6]);
}

void buildPointCubeFaceViewProj(
    const Vec3& lightPos,
    int faceIndex,
    float nearZ,
    float farZ,
    float outViewProj[16])
{
    Vec3 target = {};
    cubeFaceTarget(lightPos, faceIndex, target);
    CameraParams cam = makeCameraLookAt(lightPos, target, 90.0f, 1.0f, nearZ, farZ);
    cameraParamsToViewProj(cam, outViewProj);
}
