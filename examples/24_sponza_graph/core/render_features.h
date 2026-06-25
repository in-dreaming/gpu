#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// Incremental feature toggles for demo24 run testing.
// Default: directional light + ambient only (no shadows / points / SSGI).
struct RenderFeatures {
    bool dirLight = true;
    bool dirShadows = false;
    bool pointLights = false;
    bool pointShadows = false;
    bool gbuffer = false;
    bool lightCull = false;
    bool ssgi = false;
    bool fog = true;
    uint32_t pointLightCount = 0;
};

inline void renderFeaturesSetBase(RenderFeatures& f)
{
    f = {};
    f.dirLight = true;
    f.dirShadows = true;
    f.fog = true;
    f.pointLightCount = 0;
}

inline void renderFeaturesEnableAll(RenderFeatures& f)
{
    f.dirLight = true;
    f.dirShadows = true;
    f.pointLights = true;
    f.pointShadows = true;
    f.gbuffer = true;
    f.lightCull = true;
    f.ssgi = true;
    f.fog = true;
    f.pointLightCount = 1024;
}

inline bool renderFeaturesParseToken(RenderFeatures& f, const char* token)
{
    if (!token || !token[0]) return false;
    if (strcmp(token, "base") == 0 || strcmp(token, "dir") == 0) {
        renderFeaturesSetBase(f);
        return true;
    }
    if (strcmp(token, "all") == 0) {
        renderFeaturesEnableAll(f);
        return true;
    }
    if (strcmp(token, "shadows") == 0 || strcmp(token, "dir-shadows") == 0) {
        f.dirShadows = true;
        return true;
    }
    if (strcmp(token, "noshadows") == 0 || strcmp(token, "no-shadows") == 0) {
        f.dirShadows = false;
        return true;
    }
    if (strcmp(token, "points") == 0) {
        f.pointLights = true;
        if (f.pointLightCount == 0) f.pointLightCount = 1024;
        return true;
    }
    if (strcmp(token, "point-shadows") == 0) {
        f.pointLights = true;
        f.pointShadows = true;
        if (f.pointLightCount == 0) f.pointLightCount = 1024;
        return true;
    }
    if (strcmp(token, "gbuffer") == 0) {
        f.gbuffer = true;
        return true;
    }
    if (strcmp(token, "light-cull") == 0) {
        f.gbuffer = true;
        f.lightCull = true;
        f.pointLights = true;
        if (f.pointLightCount == 0) f.pointLightCount = 1024;
        return true;
    }
    if (strcmp(token, "ssgi") == 0) {
        f.gbuffer = true;
        f.ssgi = true;
        return true;
    }
    if (strcmp(token, "nofog") == 0) {
        f.fog = false;
        return true;
    }
    if (strncmp(token, "points=", 7) == 0) {
        int n = atoi(token + 7);
        f.pointLights = n > 0;
        f.pointLightCount = (uint32_t)(n > 0 ? n : 0);
        return true;
    }
    return false;
}

inline bool renderFeaturesParseList(RenderFeatures& f, const char* csv)
{
    renderFeaturesSetBase(f);
    if (!csv || !csv[0]) return true;

    char buf[256];
    snprintf(buf, sizeof(buf), "%s", csv);
    char* ctx = nullptr;
    char* tok = strtok_s(buf, ",", &ctx);
    bool any = false;
    while (tok) {
        while (*tok == ' ' || *tok == '\t') tok++;
        if (*tok) {
            if (!renderFeaturesParseToken(f, tok)) {
                printf("Unknown feature token: '%s'\n", tok);
                return false;
            }
            any = true;
        }
        tok = strtok_s(nullptr, ",", &ctx);
    }
    if (!any) renderFeaturesSetBase(f);
    if (f.lightCull && !f.gbuffer) f.gbuffer = true;
    if (f.ssgi && !f.gbuffer) f.gbuffer = true;
    if (f.pointShadows) {
        f.pointLights = true;
        f.dirShadows = true;
        if (f.pointLightCount == 0) f.pointLightCount = 1024;
    }
    if (f.pointLights && f.pointLightCount == 0) f.pointLightCount = 1024;
    return true;
}

inline void renderFeaturesPrint(const RenderFeatures& f)
{
    printf("Features: dir=%d shadows=%d points=%u pointShadows=%d gbuffer=%d cull=%d ssgi=%d fog=%d\n",
           f.dirLight ? 1 : 0,
           f.dirShadows ? 1 : 0,
           f.pointLightCount,
           f.pointShadows ? 1 : 0,
           f.gbuffer ? 1 : 0,
           f.lightCull ? 1 : 0,
           f.ssgi ? 1 : 0,
           f.fog ? 1 : 0);
}

inline void renderFeaturesPrintHelp()
{
    printf("Feature flags (--features <csv>):\n");
    printf("  base            directional + ambient + CSM shadows (default)\n");
    printf("  shadows         orthographic CSM for sun\n");
    printf("  noshadows       disable CSM (debug views auto re-enable)\n");
    printf("  points          point lights (default 1024, or points=N)\n");
    printf("  point-shadows   point lights + cube shadows\n");
    printf("  gbuffer         albedo/normal/depth prepass\n");
    printf("  light-cull      tiled light cull (implies gbuffer+points)\n");
    printf("  ssgi            screen-space GI (implies gbuffer)\n");
    printf("  all             enable everything\n");
    printf("  nofog           disable distance fog\n");
    printf("Examples:\n");
    printf("  24_sponza_graph --features base\n");
    printf("  24_sponza_graph --features shadows\n");
    printf("  24_sponza_graph --features shadows,points=64\n");
    printf("  24_sponza_graph --features gbuffer,ssgi\n");
    printf("  24_sponza_graph --features all\n");
}
