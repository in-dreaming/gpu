#pragma once

#include "core/camera.h"
#include "core/sponza_loader.h"

struct SimpleSceneLighting {
    float dirLightDir[3] = {0.45f, -0.88f, 0.15f};
    float dirLightIntensity = 5.0f;
    float dirLightColor[3] = {1.0f, 0.96f, 0.86f};
    float ambientColor[3] = {0.015f, 0.015f, 0.02f};
    float cameraNear = 0.4f;
    float cameraFar = 60.0f;
};

// Procedural plane + cubes for shadow debugging (no asset files).
void buildSimpleShadowScene(SponzaScene& scene);
void setupSimpleSceneCamera(FlyCamera& cam);
void setupSimpleSceneLighting(SimpleSceneLighting& out);
