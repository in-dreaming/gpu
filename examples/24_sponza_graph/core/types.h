#pragma once

#include <cstdint>
#include <cmath>

struct Vec2 { float x = 0.0f, y = 0.0f; };
struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
struct Vec4 { float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f; };

inline Vec3 vec3(float x, float y, float z) { return {x, y, z}; }
inline Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 mul(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalize(Vec3 v) { float l = sqrtf(dot(v, v)); return l > 1e-12f ? mul(v, 1.0f / l) : Vec3{0, 1, 0}; }
inline Vec3 lerp(Vec3 a, Vec3 b, float t) { return add(mul(a, 1.0f - t), mul(b, t)); }
inline Vec3 neg(Vec3 v) { return {-v.x, -v.y, -v.z}; }

struct CameraParams {
    float cameraPos[3]; float pad0;
    float right[3]; float pad1;
    float up[3]; float pad2;
    float forward[3]; float pad3;
    float proj[2]; float zParams[2];
};

struct PointLightData {
    float position[3]; float radius;
    float color[3];
    int32_t shadowMapIndex; // -1 = no shadow map; else 0..kMaxPointShadowSlots-1
    uint32_t pad0;
};

static constexpr uint32_t kMaxPointShadowSlots = 8;
static constexpr uint32_t kPointShadowMapSize = 256;
static constexpr uint32_t kCubeFaceCount = 6;
static constexpr uint32_t kShadowMapSize = 2048;

struct ShadowCascadeData {
    float viewMatrix[16];
    float projMatrix[16];
    float splitDepth;
    float texelSize;
    float pad0[2];
};

struct LightCullParams {
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t lightCount;
    uint32_t tileSize;
    CameraParams camera;
};

struct SSGIParams {
    CameraParams camera;
    float cameraPos[3];
    float pad0;
    float stepSize;
    float maxDistance;
    float thickness;
    uint32_t screenWidth;
    uint32_t screenHeight;
    uint32_t fullWidth;
    uint32_t fullHeight;
    uint32_t temporalFrame;
};

struct LightUniforms {
    CameraParams shadowCamera[4];
    float dirLightDir[3]; float dirLightIntensity;
    float dirLightColor[3]; float shadowTexelSize;
    float ambientColor[3]; float cascadeCount;
    uint32_t pointLightCount;
    PointLightData pointLights[8]; // fallback for non-bindless path
};
