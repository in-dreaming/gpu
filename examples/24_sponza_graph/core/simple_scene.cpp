#include "core/simple_scene.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static void expandBounds(SponzaScene& scene, float x, float y, float z)
{
    if (scene.vertices.empty()) {
        scene.boundsMin = {x, y, z};
        scene.boundsMax = {x, y, z};
        return;
    }
    scene.boundsMin.x = std::min(scene.boundsMin.x, x);
    scene.boundsMin.y = std::min(scene.boundsMin.y, y);
    scene.boundsMin.z = std::min(scene.boundsMin.z, z);
    scene.boundsMax.x = std::max(scene.boundsMax.x, x);
    scene.boundsMax.y = std::max(scene.boundsMax.y, y);
    scene.boundsMax.z = std::max(scene.boundsMax.z, z);
}

static uint32_t addVertex(SponzaScene& scene, float px, float py, float pz, float nx, float ny, float nz, float u, float v,
                          float material)
{
    uint32_t index = (uint32_t)scene.vertices.size();
    SponzaVertex vert = {};
    vert.position[0] = px;
    vert.position[1] = py;
    vert.position[2] = pz;
    vert.normal[0] = nx;
    vert.normal[1] = ny;
    vert.normal[2] = nz;
    vert.uv[0] = u;
    vert.uv[1] = v;
    vert.material = material;
    scene.vertices.push_back(vert);
    expandBounds(scene, px, py, pz);
    return index;
}

static void addTriangle(SponzaScene& scene, uint32_t i0, uint32_t i1, uint32_t i2)
{
    scene.indices.push_back(i0);
    scene.indices.push_back(i1);
    scene.indices.push_back(i2);
}

static void addGroundPlane(SponzaScene& scene, float halfSize, float material)
{
    const uint32_t base = (uint32_t)scene.vertices.size();
    const float y = 0.0f;
    addVertex(scene, -halfSize, y, -halfSize, 0, 1, 0, 0, 0, material);
    addVertex(scene, halfSize, y, -halfSize, 0, 1, 0, 1, 0, material);
    addVertex(scene, halfSize, y, halfSize, 0, 1, 0, 1, 1, material);
    addVertex(scene, -halfSize, y, halfSize, 0, 1, 0, 0, 1, material);
    addTriangle(scene, base + 0, base + 1, base + 2);
    addTriangle(scene, base + 0, base + 2, base + 3);
}

static void addAxisAlignedBox(SponzaScene& scene, float cx, float cy, float cz, float hx, float hy, float hz, float material)
{
    const float x0 = cx - hx, x1 = cx + hx;
    const float y0 = cy - hy, y1 = cy + hy;
    const float z0 = cz - hz, z1 = cz + hz;

    auto quad = [&](float nx, float ny, float nz, float ax, float ay, float az, float bx, float by, float bz, float cxp,
                    float cyp, float czp, float dx, float dy, float dz) {
        uint32_t v0 = addVertex(scene, ax, ay, az, nx, ny, nz, 0, 0, material);
        uint32_t v1 = addVertex(scene, bx, by, bz, nx, ny, nz, 1, 0, material);
        uint32_t v2 = addVertex(scene, cxp, cyp, czp, nx, ny, nz, 1, 1, material);
        uint32_t v3 = addVertex(scene, dx, dy, dz, nx, ny, nz, 0, 1, material);
        addTriangle(scene, v0, v1, v2);
        addTriangle(scene, v0, v2, v3);
    };

    quad(0, 1, 0, x0, y1, z0, x1, y1, z0, x1, y1, z1, x0, y1, z1);
    quad(0, -1, 0, x0, y0, z1, x1, y0, z1, x1, y0, z0, x0, y0, z0);
    quad(1, 0, 0, x1, y0, z0, x1, y1, z0, x1, y1, z1, x1, y0, z1);
    quad(-1, 0, 0, x0, y0, z1, x0, y1, z1, x0, y1, z0, x0, y0, z0);
    quad(0, 0, 1, x0, y0, z1, x1, y0, z1, x1, y1, z1, x0, y1, z1);
    quad(0, 0, -1, x1, y0, z0, x0, y0, z0, x0, y1, z0, x1, y1, z0);
}

static uint32_t beginDraw(SponzaScene& scene, uint32_t materialIndex, bool castsShadow)
{
    SponzaDraw draw = {};
    draw.firstIndex = (uint32_t)scene.indices.size();
    draw.materialIndex = materialIndex;
    draw.castsShadow = castsShadow ? 1u : 0u;
    scene.draws.push_back(draw);
    return (uint32_t)scene.draws.size() - 1;
}

static void endDraw(SponzaScene& scene, uint32_t drawSlot)
{
    SponzaDraw& draw = scene.draws[drawSlot];
    draw.indexCount = (uint32_t)scene.indices.size() - draw.firstIndex;
}

static void flyCameraLookAt(FlyCamera& cam, Vec3 target)
{
    Vec3 delta = sub(target, cam.position);
    float horiz = sqrtf(delta.x * delta.x + delta.z * delta.z);
    if (horiz > 1e-5f) {
        cam.yaw = atan2f(delta.x, delta.z);
        cam.pitch = atan2f(delta.y, horiz);
    }
    cam.pitch = std::max(-1.35f, std::min(1.35f, cam.pitch));
}

void buildSimpleShadowScene(SponzaScene& scene)
{
    scene.vertices.clear();
    scene.indices.clear();
    scene.draws.clear();
    scene.materials.clear();

    scene.materials.resize(2);
    scene.materials[0].name = "ground";
    scene.materials[1].name = "cube";

    uint32_t d = beginDraw(scene, 0, false);
    addGroundPlane(scene, 5.0f, 0.0f);
    endDraw(scene, d);

    d = beginDraw(scene, 1, true);
    addAxisAlignedBox(scene, 0.0f, 0.75f, 0.0f, 0.5f, 0.75f, 0.5f, 1.0f);
    addAxisAlignedBox(scene, -1.6f, 0.3f, 1.1f, 0.3f, 0.3f, 0.3f, 1.0f);
    addAxisAlignedBox(scene, 1.5f, 0.4f, -0.8f, 0.4f, 0.4f, 0.4f, 1.0f);
    endDraw(scene, d);
}

void setupSimpleSceneCamera(FlyCamera& cam)
{
    cam.position = {4.5f, 3.0f, -4.8f};
    flyCameraLookAt(cam, {0.0f, 0.55f, 0.0f});
}

void setupSimpleSceneLighting(SimpleSceneLighting& out)
{
    // Sun travels toward scene: high front-left, casts shadows to back-right on ground.
    out.dirLightDir[0] = 0.35f;
    out.dirLightDir[1] = -0.90f;
    out.dirLightDir[2] = 0.25f;
    out.dirLightIntensity = 4.0f;
    out.dirLightColor[0] = 1.0f;
    out.dirLightColor[1] = 0.96f;
    out.dirLightColor[2] = 0.88f;
    out.ambientColor[0] = 0.06f;
    out.ambientColor[1] = 0.06f;
    out.ambientColor[2] = 0.07f;
    out.cameraNear = 0.4f;
    out.cameraFar = 25.0f;
}
