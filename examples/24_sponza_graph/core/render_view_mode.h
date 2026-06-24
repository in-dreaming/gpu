#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

// Forward fragment debug / isolation views (see --view-mode).
enum class RenderViewMode : uint32_t {
    Final = 0,
    Albedo = 1,
    Ambient = 2,
    Diffuse = 3,
    Specular = 4,
    PointLights = 5,
    SSGI = 6,
    Shadow = 7,
    Normal = 8,
    Depth = 9,
    NdotL = 10,
    UV = 11,
    Material = 12,
    Direct = 13, // dir diffuse + points, no ambient
    Lighting = 14, // ambient + dir + points, no ssgi/fog
};

inline const char* renderViewModeName(RenderViewMode m)
{
    switch (m) {
    case RenderViewMode::Final: return "final";
    case RenderViewMode::Albedo: return "albedo";
    case RenderViewMode::Ambient: return "ambient";
    case RenderViewMode::Diffuse: return "diffuse";
    case RenderViewMode::Specular: return "specular";
    case RenderViewMode::PointLights: return "points";
    case RenderViewMode::SSGI: return "gi";
    case RenderViewMode::Shadow: return "shadow";
    case RenderViewMode::Normal: return "normal";
    case RenderViewMode::Depth: return "depth";
    case RenderViewMode::NdotL: return "ndl";
    case RenderViewMode::UV: return "uv";
    case RenderViewMode::Material: return "material";
    case RenderViewMode::Direct: return "direct";
    case RenderViewMode::Lighting: return "lighting";
    default: return "unknown";
    }
}

inline bool renderViewModeParse(const char* token, RenderViewMode& out)
{
    if (!token || !token[0]) return false;
    if (strcmp(token, "final") == 0 || strcmp(token, "0") == 0) { out = RenderViewMode::Final; return true; }
    if (strcmp(token, "albedo") == 0 || strcmp(token, "tex") == 0 || strcmp(token, "texture") == 0) {
        out = RenderViewMode::Albedo; return true;
    }
    if (strcmp(token, "ambient") == 0 || strcmp(token, "1") == 0) { out = RenderViewMode::Ambient; return true; }
    if (strcmp(token, "diffuse") == 0 || strcmp(token, "dir") == 0 || strcmp(token, "2") == 0) {
        out = RenderViewMode::Diffuse; return true;
    }
    if (strcmp(token, "specular") == 0 || strcmp(token, "spec") == 0 || strcmp(token, "3") == 0) {
        out = RenderViewMode::Specular; return true;
    }
    if (strcmp(token, "points") == 0 || strcmp(token, "point") == 0 || strcmp(token, "4") == 0) {
        out = RenderViewMode::PointLights; return true;
    }
    if (strcmp(token, "gi") == 0 || strcmp(token, "ssgi") == 0 || strcmp(token, "5") == 0) {
        out = RenderViewMode::SSGI; return true;
    }
    if (strcmp(token, "shadow") == 0 || strcmp(token, "shadows") == 0 || strcmp(token, "6") == 0) {
        out = RenderViewMode::Shadow; return true;
    }
    if (strcmp(token, "normal") == 0 || strcmp(token, "normals") == 0 || strcmp(token, "7") == 0) {
        out = RenderViewMode::Normal; return true;
    }
    if (strcmp(token, "depth") == 0 || strcmp(token, "8") == 0) { out = RenderViewMode::Depth; return true; }
    if (strcmp(token, "ndl") == 0 || strcmp(token, "ndotL") == 0 || strcmp(token, "9") == 0) {
        out = RenderViewMode::NdotL; return true;
    }
    if (strcmp(token, "uv") == 0) { out = RenderViewMode::UV; return true; }
    if (strcmp(token, "material") == 0 || strcmp(token, "mat") == 0) {
        out = RenderViewMode::Material; return true;
    }
    if (strcmp(token, "direct") == 0) { out = RenderViewMode::Direct; return true; }
    if (strcmp(token, "lighting") == 0 || strcmp(token, "lit") == 0) {
        out = RenderViewMode::Lighting; return true;
    }
    return false;
}

inline void renderViewModePrintHelp()
{
    printf("View debug modes (--view-mode <name>):\n");
    printf("  final       composite shading (default)\n");
    printf("  albedo      base color / texture only\n");
    printf("  ambient     albedo * ambient term only\n");
    printf("  diffuse     directional diffuse only\n");
    printf("  specular    directional specular highlight only\n");
    printf("  points      point light contribution only\n");
    printf("  gi          SSGI / indirect only (needs --features ssgi)\n");
    printf("  shadow      CSM shadow factor (grayscale)\n");
    printf("  normal      world normals (RGB)\n");
    printf("  depth       view-space depth ramp\n");
    printf("  ndl         sun N.L (grayscale)\n");
    printf("  uv          texture coordinates\n");
    printf("  material    material index heatmap\n");
    printf("  direct      dir + points, no ambient\n");
    printf("  lighting    ambient + dir + points, no gi/fog\n");
    printf("Examples:\n");
    printf("  24_sponza_graph --features base,nofog --view-mode albedo\n");
    printf("  24_sponza_graph --features base,nofog --view-mode ambient\n");
    printf("  24_sponza_graph --features shadows --view-mode shadow\n");
    printf("  24_sponza_graph --features gbuffer,ssgi --view-mode gi\n");
}

inline void renderViewModePrint(RenderViewMode m)
{
    printf("View mode: %s (%u)\n", renderViewModeName(m), (uint32_t)m);
}

inline RenderViewMode renderViewModeNext(RenderViewMode m)
{
    uint32_t n = ((uint32_t)m + 1u) % 15u;
    return (RenderViewMode)n;
}

inline RenderViewMode renderViewModePrev(RenderViewMode m)
{
    uint32_t n = (uint32_t)m;
    n = (n == 0u) ? 14u : (n - 1u);
    return (RenderViewMode)n;
}
