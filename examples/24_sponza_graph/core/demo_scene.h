#pragma once

#include "core/camera.h"
#include "core/dir_light_control.h"
#include "core/simple_scene.h"
#include "core/sponza_loader.h"
#include "core/types.h"
#include "render/placed_lights.h"

#include <cstdint>

enum class DemoSceneKind : uint8_t {
    Sponza = 0,
    SimpleRoom = 1,
    LightTest = 2,
};

struct DemoSceneSpace {
    float worldScale = 1.0f;
    float extent = 1.0f;
    Vec3 boundsMin = {};
    Vec3 boundsMax = {};
    Vec3 center = {};

    float placedLightRadiusScale = 0.38f;
    float placedLightRadiusMin = 4.0f;
    float placedLightRadiusCap = 16.0f;
    float placedLightForwardOffset = 0.0f;
    float pointShadowNear = 2.5f;
    bool compactRoom = false;

    static DemoSceneSpace fromBounds(const Vec3& boundsMin, const Vec3& boundsMax, DemoSceneKind kind);
};

struct DemoSceneLightingPreset {
    float dirLightDir[3] = {0.55f, -0.78f, 0.28f};
    float dirLightIntensity = 2.5f;
    float dirLightColor[3] = {1.0f, 0.94f, 0.82f};
    float ambientColor[3] = {0.22f, 0.24f, 0.28f};
    float cameraNear = 0.4f;
    float cameraFar = 2600.0f;
    float cameraFov = 60.0f;
    float cameraMoveSpeed = 60.0f;
    float cameraLookSpeed = 1.4f;
};

class DemoScene {
public:
    DemoSceneKind kind = DemoSceneKind::Sponza;
    SponzaScene geometry;
    DemoSceneSpace space;
    uint32_t builtinPointLightCount = 4;
    uint32_t lightTestCeilingCount = 4;

    bool loadSponza(const char* root);
    void buildSimpleRoom();
    void buildLightTest();

    void setupCamera(FlyCamera& cam) const;
    void setupLighting(DemoSceneLightingPreset& out) const;
    void setupDirLight(DirLightController& ctrl) const;

    bool usesSimpleMaterials() const;
    bool isLightTest() const { return kind == DemoSceneKind::LightTest; }
    bool isCompactRoom() const { return space.compactRoom; }

    uint32_t placedLightBufferOffset() const;
    uint32_t resolvePointLightUploadCount(uint32_t featureCount, const PlacedPointLights& placed) const;
    void fillBuiltinPointLights(PointLightData* lights, uint32_t count, float timeSec) const;
    void applyPlacedLights(const PlacedPointLights& placed, PointLightData* lights, uint32_t uploadCount) const;

    float placedLightRadius(const Vec3& pos) const;
    void placeLightAtCamera(PlacedPointLights& placed, uint32_t index, const CameraParams& camera,
                            PlacedLightKind lightKind) const;

    void printStartupInfo() const;

private:
    void refreshSpaceFromGeometry();
};
