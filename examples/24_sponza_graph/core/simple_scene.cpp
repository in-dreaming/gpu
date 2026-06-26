#include "core/simple_scene.h"
#include "core/dir_light_control.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

static float W(float v) { return v * kSimpleSceneWorldScale; }

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

// Open-front room shell (inward normals): ceiling + side/back walls, no -Z front face.
static void addOpenFrontInteriorShell(SponzaScene& scene, float cx, float cy, float cz, float hx, float hy, float hz,
                                      float material)
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

    quad(0, -1, 0, x0, y1, z1, x1, y1, z1, x1, y1, z0, x0, y1, z0);
    quad(0, 0, -1, x1, y0, z1, x0, y0, z1, x0, y1, z1, x1, y1, z1);
    quad(-1, 0, 0, x1, y0, z1, x1, y0, z0, x1, y1, z0, x1, y1, z1);
    quad(1, 0, 0, x0, y0, z0, x0, y0, z1, x0, y1, z1, x0, y1, z0);
}

static void addUvSphere(SponzaScene& scene, float cx, float cy, float cz, float radius, uint32_t segments, uint32_t rings,
                        float material)
{
    if (segments < 3 || rings < 2 || radius <= 0.0f) return;

    const uint32_t rowVerts = segments + 1;
    const uint32_t base = (uint32_t)scene.vertices.size();

    for (uint32_t ring = 0; ring <= rings; ring++) {
        const float v = (float)ring / (float)rings;
        const float phi = v * kPi;
        const float y = cy + radius * cosf(phi);
        const float ringR = radius * sinf(phi);
        for (uint32_t seg = 0; seg <= segments; seg++) {
            const float u = (float)seg / (float)segments;
            const float theta = u * 2.0f * kPi;
            const float x = cx + ringR * cosf(theta);
            const float z = cz + ringR * sinf(theta);
            const float nx = (x - cx) / radius;
            const float ny = (y - cy) / radius;
            const float nz = (z - cz) / radius;
            addVertex(scene, x, y, z, nx, ny, nz, u, v, material);
        }
    }

    for (uint32_t ring = 0; ring < rings; ring++) {
        for (uint32_t seg = 0; seg < segments; seg++) {
            const uint32_t i0 = base + ring * rowVerts + seg;
            const uint32_t i1 = base + ring * rowVerts + seg + 1;
            const uint32_t i2 = base + (ring + 1) * rowVerts + seg + 1;
            const uint32_t i3 = base + (ring + 1) * rowVerts + seg;
            addTriangle(scene, i0, i2, i1);
            addTriangle(scene, i0, i3, i2);
        }
    }
}

struct SimpleRng {
    uint32_t state;
    explicit SimpleRng(uint32_t seed) : state(seed) {}
    uint32_t nextU32()
    {
        state = state * 1664525u + 1013904223u;
        return state;
    }
    float next01() { return (float)(nextU32() >> 8) * (1.0f / 16777216.0f); }
    float range(float lo, float hi) { return lo + (hi - lo) * next01(); }
};

struct InteriorProp {
    float x, y, z;
    float hx, hy, hz;
    bool sphere;
    float material;
};

static bool propsOverlap(const InteriorProp& a, const InteriorProp& b, float pad)
{
    const float ax0 = a.x - a.hx - pad, ax1 = a.x + a.hx + pad;
    const float ay0 = a.y - a.hy - pad, ay1 = a.y + a.hy + pad;
    const float az0 = a.z - a.hz - pad, az1 = a.z + a.hz + pad;
    const float bx0 = b.x - b.hx - pad, bx1 = b.x + b.hx + pad;
    const float by0 = b.y - b.hy - pad, by1 = b.y + b.hy + pad;
    const float bz0 = b.z - b.hz - pad, b1 = b.z + b.hz + pad;
    return ax0 < bx1 && ax1 > bx0 && ay0 < by1 && ay1 > by0 && az0 < b1 && az1 > bz0;
}

static void scatterInteriorProps(std::vector<InteriorProp>& out, float roomHx, float roomHy, float roomHz)
{
    out.clear();
    SimpleRng rng(0xC0FFEE42u);

    const float margin = W(0.35f);
    const float minX = -roomHx + margin;
    const float maxX = roomHx - margin;
    const float minZ = -roomHz + margin * 2.2f;
    const float maxZ = roomHz - margin;
    const float maxY = roomHy - margin;
    const float pad = W(0.12f);

    constexpr uint32_t kTargetCount = 18;
    constexpr uint32_t kMaxAttempts = 96;

    for (uint32_t attempt = 0; attempt < kMaxAttempts && out.size() < kTargetCount; attempt++) {
        InteriorProp prop = {};
        const uint32_t kind = (uint32_t)(rng.next01() * 3.0f);
        if (kind == 0) {
            prop.hx = rng.range(W(0.55f), W(1.05f));
            prop.hy = rng.range(W(0.55f), W(1.35f));
            prop.hz = rng.range(W(0.55f), W(1.05f));
            prop.sphere = false;
        } else if (kind == 1) {
            const float radius = rng.range(W(0.45f), W(0.9f));
            prop.hx = prop.hy = prop.hz = radius;
            prop.sphere = true;
        } else {
            prop.hx = rng.range(W(0.65f), W(1.2f));
            prop.hy = rng.range(W(0.28f), W(0.55f));
            prop.hz = rng.range(W(0.65f), W(1.2f));
            prop.sphere = false;
        }

        prop.x = rng.range(minX + prop.hx, maxX - prop.hx);
        prop.z = rng.range(minZ + prop.hz, maxZ - prop.hz);
        prop.y = prop.hy;
        if (prop.y > maxY) continue;

        const uint32_t matPick = (uint32_t)(rng.next01() * 3.0f);
        prop.material = (matPick == 0) ? 1.0f : (matPick == 1) ? 3.0f : 4.0f;

        bool overlaps = false;
        for (const InteriorProp& placed : out) {
            if (propsOverlap(prop, placed, pad)) {
                overlaps = true;
                break;
            }
        }
        if (!overlaps) out.push_back(prop);
    }
}

static void addInteriorProps(SponzaScene& scene, const std::vector<InteriorProp>& props)
{
    for (const InteriorProp& prop : props) {
        if (prop.sphere)
            addUvSphere(scene, prop.x, prop.y, prop.z, prop.hx, 18, 12, prop.material);
        else
            addAxisAlignedBox(scene, prop.x, prop.y, prop.z, prop.hx, prop.hy, prop.hz, prop.material);
    }
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

static void setupSimpleSceneMaterials(SponzaScene& scene)
{
    scene.materials.resize(5);
    scene.materials[0].name = "floor";
    scene.materials[1].name = "prop_warm";
    scene.materials[2].name = "enclosure";
    scene.materials[3].name = "prop_cool";
    scene.materials[4].name = "prop_accent";
}

static void buildSimpleOpenRoom(SponzaScene& scene, const SimpleRoomLayout& room)
{
    uint32_t d = beginDraw(scene, 0, false);
    addGroundPlane(scene, room.halfX - W(0.15f), 0.0f);
    endDraw(scene, d);

    d = beginDraw(scene, 2, true);
    addOpenFrontInteriorShell(scene, 0.0f, room.halfY, 0.0f, room.halfX, room.halfY, room.halfZ, 2.0f);
    endDraw(scene, d);
}

static void addLightTestCluster(SponzaScene& scene, const SimpleLightTestLayout& layout)
{
    const float cx = layout.clusterCenter.x;
    const float cz = layout.clusterCenter.z;
    const float spacing = W(1.6f);
    for (int iz = -1; iz <= 1; iz++) {
        for (int ix = -1; ix <= 1; ix++) {
            const float px = cx + (float)ix * spacing;
            const float pz = cz + (float)iz * spacing;
            const float h = (ix == 0 && iz == 0) ? W(1.1f) : W(0.65f);
            const float hx = (ix == 0 && iz == 0) ? W(0.55f) : W(0.45f);
            const float hz = (ix == 0 && iz == 0) ? W(0.55f) : W(0.45f);
            addAxisAlignedBox(scene, px, h, pz, hx, h, hz, 1.0f);
        }
    }
    addAxisAlignedBox(scene, cx + W(2.2f), W(0.55f), cz + W(1.2f), W(0.4f), W(0.55f), W(0.4f), 1.0f);
    addAxisAlignedBox(scene, cx - W(2.0f), W(0.45f), cz - W(1.4f), W(0.35f), W(0.45f), W(0.35f), 1.0f);
}

void buildSimpleLightTestScene(SponzaScene& scene)
{
    scene.vertices.clear();
    scene.indices.clear();
    scene.draws.clear();
    scene.materials.clear();

    setupSimpleSceneMaterials(scene);
    const SimpleRoomLayout room = getSimpleRoomLayout();
    buildSimpleOpenRoom(scene, room);

    uint32_t d = beginDraw(scene, 1, true);
    addLightTestCluster(scene, getSimpleLightTestLayout());
    endDraw(scene, d);
}

SimpleLightTestLayout getSimpleLightTestLayout()
{
    const SimpleRoomLayout room = getSimpleRoomLayout();
    SimpleLightTestLayout layout = {};
    layout.clusterCenter = {0.0f, W(0.85f), 0.0f};
    layout.clusterHalfExtent = W(1.3f);
    layout.lightHeight = room.halfY * 2.0f - W(1.35f);
    layout.lightRadius = room.halfY * 0.65f;
    layout.lightPairSeparation = W(1.1f);
    return layout;
}

SimpleRoomLayout getSimpleRoomLayout()
{
    return {};
}

void setupSimpleRoomExteriorCamera(FlyCamera& cam)
{
    const SimpleRoomLayout room = getSimpleRoomLayout();
    const float eyeY = room.halfY * 0.48f;
    const float standoff = room.halfZ + W(3.4f);
    cam.position = {0.0f, eyeY, -standoff};
    flyCameraLookAt(cam, {0.0f, room.halfY * 0.36f, room.halfZ * 0.08f});
}

void setupSimpleSceneCamera(FlyCamera& cam)
{
    setupSimpleRoomExteriorCamera(cam);
}

void setupSimpleLightTestCamera(FlyCamera& cam)
{
    const SimpleLightTestLayout layout = getSimpleLightTestLayout();
    cam.position = {-4.0f, 26.0f, -92.0f};
    cam.yaw = 0.08f;
    Vec3 delta = sub(layout.clusterCenter, cam.position);
    float horiz = sqrtf(delta.x * delta.x + delta.z * delta.z);
    if (horiz > 1e-5f)
        cam.pitch = atan2f(delta.y, horiz);
    cam.pitch = std::max(-1.35f, std::min(1.35f, cam.pitch));
}

void setupSimpleLightTestDirLight(DirLightController& ctrl)
{
    ctrl.yaw = 0.0f;
    ctrl.pitch = 1.52f;
}

void setupSimpleLightTestLighting(SimpleSceneLighting& out)
{
    const SimpleRoomLayout room = getSimpleRoomLayout();
    out.dirLightDir[0] = 0.35f;
    out.dirLightDir[1] = -0.90f;
    out.dirLightDir[2] = 0.25f;
    out.dirLightIntensity = 1.0f;
    out.dirLightColor[0] = 1.0f;
    out.dirLightColor[1] = 0.96f;
    out.dirLightColor[2] = 0.88f;
    out.ambientColor[0] = 0.03f;
    out.ambientColor[1] = 0.03f;
    out.ambientColor[2] = 0.035f;
    out.cameraNear = W(0.35f);
    out.cameraFar = room.halfZ * 4.5f + W(3.0f);
    out.cameraFov = 50.0f;
    out.cameraMoveSpeed = 8.0f * kSimpleSceneWorldScale;
    out.cameraLookSpeed = 1.4f;
}

void buildSimpleShadowScene(SponzaScene& scene)
{
    scene.vertices.clear();
    scene.indices.clear();
    scene.draws.clear();
    scene.materials.clear();

    setupSimpleSceneMaterials(scene);
    const SimpleRoomLayout room = getSimpleRoomLayout();
    buildSimpleOpenRoom(scene, room);

    std::vector<InteriorProp> props;
    scatterInteriorProps(props, room.halfX, room.halfY, room.halfZ);

    uint32_t d = beginDraw(scene, 1, true);
    addInteriorProps(scene, props);
    endDraw(scene, d);
}

void setupSimpleSceneLighting(SimpleSceneLighting& out)
{
    const SimpleRoomLayout room = getSimpleRoomLayout();
    // Sun travels toward scene: high front-left, casts shadows to back-right on ground.
    out.dirLightDir[0] = 0.35f;
    out.dirLightDir[1] = -0.90f;
    out.dirLightDir[2] = 0.25f;
    out.dirLightIntensity = 1.2f;
    out.dirLightColor[0] = 1.0f;
    out.dirLightColor[1] = 0.96f;
    out.dirLightColor[2] = 0.88f;
    out.ambientColor[0] = 0.025f;
    out.ambientColor[1] = 0.025f;
    out.ambientColor[2] = 0.03f;
    out.cameraNear = W(0.35f);
    out.cameraFar = room.halfZ * 4.5f + W(3.0f);
    out.cameraFov = 50.0f;
    out.cameraMoveSpeed = 8.0f * kSimpleSceneWorldScale;
    out.cameraLookSpeed = 1.4f;
}
