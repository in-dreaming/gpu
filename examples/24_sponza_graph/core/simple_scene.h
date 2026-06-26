#pragma once

#include "core/camera.h"
#include "core/dir_light_control.h"
#include "core/sponza_loader.h"
#include "core/types.h"

static constexpr float kSimpleSceneWorldScale = 10.0f;
// Note: uniform W() scaling changes fly speed vs world meters, not on-screen size. Visual scale comes from
// camera distance, FOV, and prop size relative to the room.

struct SimpleSceneLighting {
    float dirLightDir[3] = {0.45f, -0.88f, 0.15f};
    float dirLightIntensity = 5.0f;
    float dirLightColor[3] = {1.0f, 0.96f, 0.86f};
    float ambientColor[3] = {0.015f, 0.015f, 0.02f};
    float cameraNear = 0.4f;
    float cameraFar = 60.0f;
    float cameraFov = 60.0f;
    float cameraMoveSpeed = 60.0f;
    float cameraLookSpeed = 1.4f;
};

// Layout for --light-test: tight object cluster + overhead point lights.
struct SimpleLightTestLayout {
    Vec3 clusterCenter = {0.0f, 0.35f * kSimpleSceneWorldScale, 0.0f};
    float clusterHalfExtent = 1.15f * kSimpleSceneWorldScale;
    float lightHeight = 2.4f * kSimpleSceneWorldScale;
    float lightRadius = 1.35f * kSimpleSceneWorldScale;
    float lightPairSeparation = 0.55f * kSimpleSceneWorldScale;
};

struct SimpleRoomLayout {
    float halfX = 3.2f * kSimpleSceneWorldScale;
    float halfY = 2.8f * kSimpleSceneWorldScale;
    float halfZ = 3.2f * kSimpleSceneWorldScale;
};

SimpleLightTestLayout getSimpleLightTestLayout();
SimpleRoomLayout getSimpleRoomLayout();

// Procedural open-front room + scattered props for shadow debugging (no asset files).
void buildSimpleShadowScene(SponzaScene& scene);
// Dense cube cluster for isolated point / point-shadow light testing.
void buildSimpleLightTestScene(SponzaScene& scene);
void setupSimpleRoomExteriorCamera(FlyCamera& cam);
void setupSimpleSceneCamera(FlyCamera& cam);
void setupSimpleLightTestCamera(FlyCamera& cam);
void setupSimpleLightTestDirLight(DirLightController& ctrl);
void setupSimpleSceneLighting(SimpleSceneLighting& out);
void setupSimpleLightTestLighting(SimpleSceneLighting& out);
