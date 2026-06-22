#include "sponza_loader.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cfloat>
#include <cstring>

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static uint32_t addMaterial(SponzaScene& scene, const std::string& name) {
    for (uint32_t i = 0; i < (uint32_t)scene.materials.size(); ++i)
        if (scene.materials[i].name == name) return i;
    SponzaMaterial m; m.name = name; scene.materials.push_back(m);
    return (uint32_t)scene.materials.size() - 1;
}

bool parseMtl(const char* path, SponzaScene& scene) {
    std::ifstream file(path); if (!file) return false;
    std::string line; uint32_t cur = UINT32_MAX;
    while (std::getline(file, line)) {
        line = trim(line); if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line); std::string tag; ss >> tag;
        if (tag == "newmtl") { std::string name; ss >> name; cur = addMaterial(scene, name); }
        else if (tag == "map_Kd" && cur != UINT32_MAX) { std::string rest; std::getline(ss, rest); scene.materials[cur].baseColor = trim(rest); }
        else if ((tag == "map_bump" || tag == "bump") && cur != UINT32_MAX) { std::string rest; std::getline(ss, rest); scene.materials[cur].normal = trim(rest); }
    }
    return true;
}

struct ObjKey { int p, t, n; uint32_t m; bool operator==(const ObjKey& o) const { return p == o.p && t == o.t && n == o.n && m == o.m; } };
struct ObjKeyHash { size_t operator()(const ObjKey& k) const { uint64_t h = 1469598103934665603ull; auto mix = [&](uint32_t v) { h = (h ^ v) * 1099511628211ull; }; mix((uint32_t)k.p); mix((uint32_t)k.t); mix((uint32_t)k.n); mix(k.m); return (size_t)h; } };

static int resolveObjIndex(int idx, int count) { return idx > 0 ? idx - 1 : idx < 0 ? count + idx : -1; }

bool parseObj(const char* path, SponzaScene& scene) {
    std::ifstream file(path); if (!file) return false;
    std::vector<Vec3> positions, normals;
    std::vector<Vec2> texcoords;
    std::unordered_map<ObjKey, uint32_t, ObjKeyHash> vmap;
    uint32_t activeMat = addMaterial(scene, "default");
    SponzaDraw* activeDraw = nullptr;
    scene.boundsMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    scene.boundsMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
    std::string line;
    while (std::getline(file, line)) {
        line = trim(line); if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line); std::string tag; ss >> tag;
        if (tag == "v") {
            Vec3 p; ss >> p.x >> p.y >> p.z; positions.push_back(p);
            scene.boundsMin.x = std::min(scene.boundsMin.x, p.x);
            scene.boundsMin.y = std::min(scene.boundsMin.y, p.y);
            scene.boundsMin.z = std::min(scene.boundsMin.z, p.z);
            scene.boundsMax.x = std::max(scene.boundsMax.x, p.x);
            scene.boundsMax.y = std::max(scene.boundsMax.y, p.y);
            scene.boundsMax.z = std::max(scene.boundsMax.z, p.z);
        } else if (tag == "vn") { Vec3 n; ss >> n.x >> n.y >> n.z; normals.push_back(normalize(n)); }
        else if (tag == "vt") { Vec2 uv; ss >> uv.x >> uv.y; texcoords.push_back(uv); }
        else if (tag == "usemtl") { std::string name; ss >> name; activeMat = addMaterial(scene, name); activeDraw = nullptr; }
        else if (tag == "f") {
            std::vector<uint32_t> poly; std::string tok;
            while (ss >> tok) {
                ObjKey key = {-1, -1, -1, activeMat}; int vals[3] = {0, 0, 0}; int vi = 0; std::string cur;
                for (size_t i = 0; i <= tok.size(); ++i) {
                    char c = i < tok.size() ? tok[i] : '/';
                    if (c == '/') { if (!cur.empty() && vi < 3) vals[vi] = atoi(cur.c_str()); cur.clear(); vi++; }
                    else cur.push_back(c);
                }
                key.p = resolveObjIndex(vals[0], (int)positions.size());
                key.t = resolveObjIndex(vals[1], (int)texcoords.size());
                key.n = resolveObjIndex(vals[2], (int)normals.size());
                if (key.p < 0 || key.p >= (int)positions.size()) continue;
                auto it = vmap.find(key); uint32_t vi2;
                if (it == vmap.end()) {
                    Vec3 p = positions[key.p];
                    Vec3 n = (key.n >= 0 && key.n < (int)normals.size()) ? normals[key.n] : Vec3{0, 1, 0};
                    Vec2 uv = (key.t >= 0 && key.t < (int)texcoords.size()) ? texcoords[key.t] : Vec2{0, 0};
                    SponzaVertex v = {{p.x, p.y, p.z}, {n.x, n.y, n.z}, {uv.x, uv.y}, (float)activeMat};
                    vi2 = (uint32_t)scene.vertices.size(); scene.vertices.push_back(v); vmap.emplace(key, vi2);
                } else vi2 = it->second;
                poly.push_back(vi2);
            }
            if (poly.size() >= 3) {
                if (!activeDraw) { scene.draws.push_back({}); activeDraw = &scene.draws.back(); activeDraw->firstIndex = (uint32_t)scene.indices.size(); activeDraw->materialIndex = activeMat; }
                for (size_t i = 1; i + 1 < poly.size(); ++i) {
                    scene.indices.push_back(poly[0]); scene.indices.push_back(poly[i]); scene.indices.push_back(poly[i + 1]);
                    activeDraw->indexCount += 3;
                }
            }
        }
    }
    for (size_t i = 0; i < scene.indices.size(); i += 3) {
        SponzaVertex& a = scene.vertices[scene.indices[i]];
        SponzaVertex& b = scene.vertices[scene.indices[i + 1]];
        SponzaVertex& c = scene.vertices[scene.indices[i + 2]];
        Vec3 pa = {a.position[0], a.position[1], a.position[2]};
        Vec3 pb = {b.position[0], b.position[1], b.position[2]};
        Vec3 pc = {c.position[0], c.position[1], c.position[2]};
        Vec3 face = normalize(cross(sub(pb, pa), sub(pc, pa)));
        if (std::fabs(a.normal[0]) + std::fabs(a.normal[1]) + std::fabs(a.normal[2]) < 0.01f) {
            a.normal[0] = face.x; a.normal[1] = face.y; a.normal[2] = face.z;
            b.normal[0] = face.x; b.normal[1] = face.y; b.normal[2] = face.z;
            c.normal[0] = face.x; c.normal[1] = face.y; c.normal[2] = face.z;
        }
    }
    return !scene.vertices.empty() && !scene.indices.empty();
}
