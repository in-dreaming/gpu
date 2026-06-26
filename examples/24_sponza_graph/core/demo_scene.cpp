#include "core/demo_scene.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static constexpr float kDegToRad = 3.14159265f / 180.0f;

static void initOmniPointLight(PointLightData& light)
{
    light.spotEnable = 0u;
    light.direction[0] = 0.0f;
    light.direction[1] = -1.0f;
    light.direction[2] = 0.0f;
    light.innerConeCos = 1.0f;
    light.outerConeCos = 0.0f;
}

static void initSpotPointLight(PointLightData& light, float dirX, float dirY, float dirZ, float innerDeg, float outerDeg)
{
    const float len = sqrtf(dirX * dirX + dirY * dirY + dirZ * dirZ);
    light.spotEnable = 1u;
    if (len > 1e-5f) {
        light.direction[0] = dirX / len;
        light.direction[1] = dirY / len;
        light.direction[2] = dirZ / len;
    } else {
        light.direction[0] = 0.0f;
        light.direction[1] = -1.0f;
        light.direction[2] = 0.0f;
    }
    light.innerConeCos = cosf(innerDeg * kDegToRad);
    light.outerConeCos = cosf(outerDeg * kDegToRad);
}

static void zeroPointLight(PointLightData& light)
{
    light = {};
    initOmniPointLight(light);
    light.shadowMapIndex = -1;
}

static void fillLightTestCeilingLights(PointLightData* lights, uint32_t count, const SimpleLightTestLayout& layout)
{
    const float cx = layout.clusterCenter.x;
    const float cz = layout.clusterCenter.z;
    const float y = layout.lightHeight;
    const float r = layout.lightRadius;
    const float spread = layout.lightPairSeparation * 0.5f;

    static const float kColors[4][3] = {
        {2.8f, 0.5f, 0.32f},
        {0.32f, 0.58f, 2.8f},
        {2.4f, 1.1f, 0.35f},
        {0.35f, 2.0f, 2.2f},
    };

    for (uint32_t i = 0; i < count; i++) {
        lights[i].shadowMapIndex = -1;
        lights[i].radius = r;
        if (count == 1) {
            lights[i].position[0] = cx + spread * 0.35f;
            lights[i].position[1] = y;
            lights[i].position[2] = cz + spread * 0.2f;
        } else if (count == 2) {
            lights[i].position[0] = cx + ((i == 0) ? -spread : spread);
            lights[i].position[1] = y;
            lights[i].position[2] = cz;
        } else {
            const float sx = (i & 1u) ? spread : -spread;
            const float sz = (i < 2u) ? -spread * 0.65f : spread * 0.65f;
            lights[i].position[0] = cx + sx;
            lights[i].position[1] = y;
            lights[i].position[2] = cz + sz;
        }
        lights[i].color[0] = kColors[i % 4][0];
        lights[i].color[1] = kColors[i % 4][1];
        lights[i].color[2] = kColors[i % 4][2];

        if ((i & 1u) == 0u)
            initOmniPointLight(lights[i]);
        else
            initSpotPointLight(lights[i], cx - lights[i].position[0], layout.clusterCenter.y - y, cz - lights[i].position[2],
                               32.0f, 48.0f);
    }
}

static void fillSimpleInteriorLights(PointLightData* lights, uint32_t count, const SimpleRoomLayout& room)
{
    const float ceilingY = room.halfY * 2.0f - kSimpleSceneWorldScale * 0.55f;
    const float reach = room.halfY * 1.05f;
    const float spreadX = room.halfX * 0.38f;
    const float spreadZ = room.halfZ * 0.28f;

    static const float kColors[4][3] = {
        {0.95f, 0.2f, 0.14f},
        {0.14f, 0.24f, 0.95f},
        {0.85f, 0.42f, 0.12f},
        {0.12f, 0.72f, 0.55f},
    };

    for (uint32_t i = 0; i < count; i++) {
        lights[i].shadowMapIndex = -1;
        lights[i].radius = reach;
        if (count == 1) {
            lights[i].position[0] = 0.0f;
            lights[i].position[1] = ceilingY;
            lights[i].position[2] = room.halfZ * 0.1f;
        } else if (count == 2) {
            lights[i].position[0] = (i == 0) ? -spreadX : spreadX;
            lights[i].position[1] = ceilingY;
            lights[i].position[2] = (i == 0) ? -room.halfZ * 0.12f : room.halfZ * 0.18f;
        } else {
            const float sx = (i & 1u) ? spreadX : -spreadX;
            const float sz = (i < 2u) ? -spreadZ : spreadZ;
            lights[i].position[0] = sx;
            lights[i].position[1] = ceilingY;
            lights[i].position[2] = sz;
        }
        lights[i].color[0] = kColors[i % 4][0];
        lights[i].color[1] = kColors[i % 4][1];
        lights[i].color[2] = kColors[i % 4][2];

        if ((i & 1u) == 0u)
            initOmniPointLight(lights[i]);
        else
            initSpotPointLight(lights[i], 0.0f, -1.0f, 0.0f, 35.0f, 52.0f);
    }
}

static void fillSponzaOrbitalLights(PointLightData* lights, uint32_t count, const DemoSceneSpace& space, float timeSec)
{
    const float orbitRadius = std::max(space.extent * 0.55f, 6.0f);
    const float heightBase = space.center.y + space.extent * 0.42f;
    const float heightVarAmp = space.extent * 0.12f;
    const float lightRadius = std::max(space.extent * 0.32f, 10.0f);

    for (uint32_t i = 0; i < count; i++) {
        const float angle = (float)i / (float)count * 6.28318f + timeSec * 0.2f;
        const float angle2 = (float)(i * 7 % 773) / 773.0f * 6.28318f + timeSec * 0.3f;
        const float ring = orbitRadius * (0.85f + 0.15f * (float)(i % 3) / 2.0f);
        const float heightVar = sinf(angle2) * heightVarAmp;
        lights[i].position[0] = space.center.x + cosf(angle) * ring;
        lights[i].position[1] = heightBase + heightVar;
        lights[i].position[2] = space.center.z + sinf(angle) * ring;
        lights[i].radius = lightRadius * (0.9f + 0.1f * (float)(i % 4));
        const float hue = (float)i / (float)count;
        lights[i].color[0] = 0.35f + 0.30f * sinf(hue * 6.28318f + timeSec * 0.5f);
        lights[i].color[1] = 0.35f + 0.30f * sinf((hue + 0.33f) * 6.28318f + timeSec * 0.5f);
        lights[i].color[2] = 0.35f + 0.30f * sinf((hue + 0.67f) * 6.28318f + timeSec * 0.5f);
        lights[i].shadowMapIndex = -1;
        initOmniPointLight(lights[i]);
    }
}

DemoSceneSpace DemoSceneSpace::fromBounds(const Vec3& boundsMin, const Vec3& boundsMax, DemoSceneKind kind)
{
    DemoSceneSpace space = {};
    space.boundsMin = boundsMin;
    space.boundsMax = boundsMax;
    space.center = {
        (boundsMin.x + boundsMax.x) * 0.5f,
        (boundsMin.y + boundsMax.y) * 0.5f,
        (boundsMin.z + boundsMax.z) * 0.5f,
    };
    const float extentX = boundsMax.x - boundsMin.x;
    const float extentY = boundsMax.y - boundsMin.y;
    const float extentZ = boundsMax.z - boundsMin.z;
    space.extent = std::max(std::max(extentX, extentY), std::max(extentZ, 4.0f));

    if (kind == DemoSceneKind::LightTest || kind == DemoSceneKind::SimpleRoom) {
        space.worldScale = kSimpleSceneWorldScale;
        space.compactRoom = true;
        space.placedLightRadiusScale = 0.38f;
        space.placedLightRadiusMin = 4.0f;
        space.placedLightRadiusCap = kSimpleSceneWorldScale * 1.6f;
        space.placedLightForwardOffset = 0.0f;
        space.pointShadowNear = kSimpleSceneWorldScale * 0.08f;
    } else {
        space.worldScale = space.extent;
        space.compactRoom = false;
        space.placedLightRadiusScale = 0.22f;
        space.placedLightRadiusMin = std::max(8.0f, space.extent * 0.02f);
        space.placedLightRadiusCap = std::max(48.0f, space.extent * 0.08f);
        space.placedLightForwardOffset = std::max(1.5f, space.extent * 0.0015f);
        space.pointShadowNear = std::max(0.5f, space.extent * 0.001f);
    }
    return space;
}

void DemoScene::refreshSpaceFromGeometry()
{
    space = DemoSceneSpace::fromBounds(geometry.boundsMin, geometry.boundsMax, kind);
}

bool DemoScene::loadSponza(const char* root)
{
    kind = DemoSceneKind::Sponza;
    builtinPointLightCount = 4;
    geometry.vertices.clear();
    geometry.indices.clear();
    geometry.draws.clear();
    geometry.materials.clear();

    char objPath[1024], mtlPath[1024];
    snprintf(objPath, sizeof(objPath), "%s/sponza.obj", root);
    snprintf(mtlPath, sizeof(mtlPath), "%s/sponza.mtl", root);
    if (!parseMtl(mtlPath, geometry) || !parseObj(objPath, geometry))
        return false;
    refreshSpaceFromGeometry();
    return true;
}

void DemoScene::buildSimpleRoom()
{
    kind = DemoSceneKind::SimpleRoom;
    builtinPointLightCount = 4;
    buildSimpleShadowScene(geometry);
    refreshSpaceFromGeometry();
}

void DemoScene::buildLightTest()
{
    kind = DemoSceneKind::LightTest;
    builtinPointLightCount = 4;
    buildSimpleLightTestScene(geometry);
    refreshSpaceFromGeometry();
}

void DemoScene::setupCamera(FlyCamera& cam) const
{
    if (kind == DemoSceneKind::LightTest)
        setupSimpleLightTestCamera(cam);
    else if (kind == DemoSceneKind::SimpleRoom)
        setupSimpleSceneCamera(cam);
    else {
        cam.position = {321.0f, 733.0f, -40.0f};
        cam.yaw = -1.62f;
        cam.pitch = 0.0f;
    }
}

void DemoScene::setupLighting(DemoSceneLightingPreset& out) const
{
    if (kind == DemoSceneKind::LightTest) {
        SimpleSceneLighting preset = {};
        setupSimpleLightTestLighting(preset);
        memcpy(out.dirLightDir, preset.dirLightDir, sizeof(out.dirLightDir));
        memcpy(out.dirLightColor, preset.dirLightColor, sizeof(out.dirLightColor));
        memcpy(out.ambientColor, preset.ambientColor, sizeof(out.ambientColor));
        out.dirLightIntensity = preset.dirLightIntensity;
        out.cameraNear = preset.cameraNear;
        out.cameraFar = preset.cameraFar;
        out.cameraFov = preset.cameraFov;
        out.cameraMoveSpeed = preset.cameraMoveSpeed;
        out.cameraLookSpeed = preset.cameraLookSpeed;
        return;
    }
    if (kind == DemoSceneKind::SimpleRoom) {
        SimpleSceneLighting preset = {};
        setupSimpleSceneLighting(preset);
        memcpy(out.dirLightDir, preset.dirLightDir, sizeof(out.dirLightDir));
        memcpy(out.dirLightColor, preset.dirLightColor, sizeof(out.dirLightColor));
        memcpy(out.ambientColor, preset.ambientColor, sizeof(out.ambientColor));
        out.dirLightIntensity = preset.dirLightIntensity;
        out.cameraNear = preset.cameraNear;
        out.cameraFar = preset.cameraFar;
        out.cameraFov = preset.cameraFov;
        out.cameraMoveSpeed = preset.cameraMoveSpeed;
        out.cameraLookSpeed = preset.cameraLookSpeed;
        return;
    }

    out.dirLightDir[0] = 0.55f;
    out.dirLightDir[1] = -0.78f;
    out.dirLightDir[2] = 0.28f;
    out.dirLightIntensity = 2.5f;
    out.dirLightColor[0] = 1.0f;
    out.dirLightColor[1] = 0.94f;
    out.dirLightColor[2] = 0.82f;
    out.ambientColor[0] = 0.22f;
    out.ambientColor[1] = 0.24f;
    out.ambientColor[2] = 0.28f;
    out.cameraNear = 0.4f;
    out.cameraFar = std::max(space.extent * 6.0f, 800.0f);
    out.cameraFov = 60.0f;
    out.cameraMoveSpeed = std::max(30.0f, space.extent * 0.04f);
    out.cameraLookSpeed = 1.4f;
}

void DemoScene::setupDirLight(DirLightController& ctrl) const
{
    if (kind == DemoSceneKind::LightTest)
        setupSimpleLightTestDirLight(ctrl);
    else
        ctrl.yaw = 0.0f;
}

bool DemoScene::usesSimpleMaterials() const
{
    return kind != DemoSceneKind::Sponza;
}

uint32_t DemoScene::placedLightBufferOffset() const
{
    if (kind == DemoSceneKind::LightTest)
        return std::max(1u, lightTestCeilingCount);
    return 0u;
}

uint32_t DemoScene::resolvePointLightUploadCount(uint32_t featureCount, const PlacedPointLights& placed) const
{
    const uint32_t placedCount = placed.requiredLightCount();
    if (kind == DemoSceneKind::LightTest) {
        const uint32_t base = std::max(1u, lightTestCeilingCount);
        return std::max({featureCount, base + placedCount});
    }
    if (placedCount > 0) {
        if (kind == DemoSceneKind::Sponza)
            return placedCount;
        return std::max(featureCount, placedCount);
    }
    return std::max(featureCount, builtinPointLightCount);
}

void DemoScene::fillBuiltinPointLights(PointLightData* lights, uint32_t count, float timeSec) const
{
    if (count == 0) return;

    if (kind == DemoSceneKind::LightTest) {
        const SimpleLightTestLayout layout = getSimpleLightTestLayout();
        const uint32_t base = std::max(1u, lightTestCeilingCount);
        fillLightTestCeilingLights(lights, std::min(count, base), layout);
        for (uint32_t i = base; i < count; i++)
            zeroPointLight(lights[i]);
        return;
    }

    if (kind == DemoSceneKind::SimpleRoom) {
        const SimpleRoomLayout room = getSimpleRoomLayout();
        const uint32_t active = std::min(count, builtinPointLightCount);
        fillSimpleInteriorLights(lights, active, room);
        for (uint32_t i = active; i < count; i++)
            zeroPointLight(lights[i]);
        return;
    }

    fillSponzaOrbitalLights(lights, count, space, timeSec);
}

void DemoScene::applyPlacedLights(const PlacedPointLights& placed, PointLightData* lights, uint32_t uploadCount) const
{
    if (kind == DemoSceneKind::Sponza && placed.requiredLightCount() > 0) {
        for (uint32_t i = 0; i < uploadCount; i++)
            zeroPointLight(lights[i]);
        for (uint32_t pi = 0; pi < PlacedPointLights::kMaxSlots; pi++) {
            if (!placed.slots[pi].active || pi >= uploadCount) continue;
            lights[pi] = placed.slots[pi].data;
        }
        return;
    }

    const uint32_t offset = placedLightBufferOffset();
    for (uint32_t pi = 0; pi < PlacedPointLights::kMaxSlots; pi++) {
        if (!placed.slots[pi].active) continue;
        const uint32_t li = offset + pi;
        if (li >= uploadCount) break;
        lights[li] = placed.slots[pi].data;
    }
}

float DemoScene::placedLightRadius(const Vec3& lightPos) const
{
    const float corners[8][3] = {
        {space.boundsMin.x, space.boundsMin.y, space.boundsMin.z},
        {space.boundsMax.x, space.boundsMin.y, space.boundsMin.z},
        {space.boundsMin.x, space.boundsMax.y, space.boundsMin.z},
        {space.boundsMax.x, space.boundsMax.y, space.boundsMin.z},
        {space.boundsMin.x, space.boundsMin.y, space.boundsMax.z},
        {space.boundsMax.x, space.boundsMin.y, space.boundsMax.z},
        {space.boundsMin.x, space.boundsMax.y, space.boundsMax.z},
        {space.boundsMax.x, space.boundsMax.y, space.boundsMax.z},
    };

    float maxDist = space.placedLightRadiusMin;
    for (int i = 0; i < 8; i++) {
        const float dx = corners[i][0] - lightPos.x;
        const float dy = corners[i][1] - lightPos.y;
        const float dz = corners[i][2] - lightPos.z;
        maxDist = std::max(maxDist, sqrtf(dx * dx + dy * dy + dz * dz));
    }
    const float scaled = maxDist * space.placedLightRadiusScale;
    return std::max(space.placedLightRadiusMin, std::min(scaled, space.placedLightRadiusCap));
}

void DemoScene::placeLightAtCamera(PlacedPointLights& placed, uint32_t index, const CameraParams& camera,
                                   PlacedLightKind lightKind) const
{
    if (index >= PlacedPointLights::kMaxSlots) return;

    PlacedPointLightSlot& slot = placed.slots[index];
    slot.active = true;

    float fx = camera.forward[0];
    float fy = camera.forward[1];
    float fz = camera.forward[2];
    const float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    if (flen > 1e-5f) {
        fx /= flen;
        fy /= flen;
        fz /= flen;
    } else {
        fx = 0.0f;
        fy = 0.0f;
        fz = 1.0f;
    }

    slot.data.position[0] = camera.cameraPos[0] + fx * space.placedLightForwardOffset;
    slot.data.position[1] = camera.cameraPos[1] + fy * space.placedLightForwardOffset;
    slot.data.position[2] = camera.cameraPos[2] + fz * space.placedLightForwardOffset;
    const Vec3 lightPos = {slot.data.position[0], slot.data.position[1], slot.data.position[2]};
    slot.data.radius = placedLightRadius(lightPos);
    slot.data.shadowMapIndex = -1;

    static const float kPalette[9][3] = {
        {1.0f, 0.0f, 0.0f},
        {0.00f, 0.9f, 0.00f},
        {0.00f, 0.0f, 1.0f},
        {0.85f, 0.85f, 0.85f},
        {1.0f, 0.55f, 0.1f},
        {0.55f, 0.2f, 1.0f},
        {1.0f, 0.25f, 0.65f},
        {0.2f, 0.85f, 0.85f},
        {0.95f, 0.95f, 0.55f},
    };
    const uint32_t pi = index % 9u;
    slot.data.color[0] = kPalette[pi][0];
    slot.data.color[1] = kPalette[pi][1];
    slot.data.color[2] = kPalette[pi][2];

    slot.forward[0] = fx;
    slot.forward[1] = fy;
    slot.forward[2] = fz;

    if (lightKind == PlacedLightKind::Omni) {
        initOmniPointLight(slot.data);
        printf("Placed omni point light %u at (%.2f, %.2f, %.2f) radius=%.2f color=(%.2f, %.2f, %.2f)\n", index + 1,
               slot.data.position[0], slot.data.position[1], slot.data.position[2], slot.data.radius,
               slot.data.color[0], slot.data.color[1], slot.data.color[2]);
    } else {
        initSpotPointLight(slot.data, fx, fy, fz, 28.0f, 42.0f);
        printf("Placed spotlight %u at (%.2f, %.2f, %.2f) radius=%.2f dir=(%.2f, %.2f, %.2f), color=(%.2f, %.2f, %.2f)\n",
               index + 1, slot.data.position[0], slot.data.position[1], slot.data.position[2], slot.data.radius,
               slot.data.direction[0], slot.data.direction[1], slot.data.direction[2], slot.data.color[0],
               slot.data.color[1], slot.data.color[2]);
    }
}

void DemoScene::printStartupInfo() const
{
    if (kind == DemoSceneKind::LightTest) {
        printf("Light-test scene (open room + cube cluster): %zu verts, %zu indices, %zu draws\n",
               geometry.vertices.size(), geometry.indices.size(), geometry.draws.size());
    } else if (kind == DemoSceneKind::SimpleRoom) {
        printf("Simple scene (open room + scattered props): %zu verts, %zu indices, %zu draws\n",
               geometry.vertices.size(), geometry.indices.size(), geometry.draws.size());
    } else {
        printf("Sponza: %zu verts, %zu indices, %zu materials, %zu draws\n", geometry.vertices.size(),
               geometry.indices.size(), geometry.materials.size(), geometry.draws.size());
    }

    if (isCompactRoom()) {
        const SimpleRoomLayout room = getSimpleRoomLayout();
        printf("Room shell: %.0fx%.0fx%.0fm (scale=%.0f), bounds (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f)\n",
               room.halfX * 2.0f, room.halfY * 2.0f, room.halfZ * 2.0f, kSimpleSceneWorldScale, geometry.boundsMin.x,
               geometry.boundsMin.y, geometry.boundsMin.z, geometry.boundsMax.x, geometry.boundsMax.y,
               geometry.boundsMax.z);
        if (kind == DemoSceneKind::LightTest)
            printf("  --light-test: center cube cluster. Use --simple only for random props inside the room.\n");
    } else {
        printf("Scene extent: %.1fm, bounds (%.0f,%.0f,%.0f)-(%.0f,%.0f,%.0f)\n", space.extent, geometry.boundsMin.x,
               geometry.boundsMin.y, geometry.boundsMin.z, geometry.boundsMax.x, geometry.boundsMax.y,
               geometry.boundsMax.z);
    }
}
