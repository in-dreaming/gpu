// Forward pass with runtime feature toggles (see --features CLI)

struct CameraParams
{
    float3 cameraPos;
    float pad0;
    float3 right;
    float pad1;
    float3 up;
    float pad2;
    float3 forward;
    float pad3;
    float2 proj;
    float2 zParams;
};

struct PointLightData
{
    float3 position;
    float radius;
    float3 color;
    int shadowMapIndex;
    uint pad0;
};

struct ShadowCascadeData
{
    float splitFar;
    float texelSize;
    float2 pad;
};

struct ForwardFeatureFlags
{
    uint enableDirLight;
    uint enableDirShadow;
    uint enablePointLights;
    uint enableSSGI;
    uint enableFog;
    uint pointLightCount;
};

struct ForwardLighting
{
    float3 dirLightDir;
    float dirLightIntensity;
    float3 dirLightColor;
    float pad0;
    float3 ambientColor;
    float pad1;
};

struct ForwardParams
{
    CameraParams camera;
    ForwardFeatureFlags features;
    uint2 padAfterFeatures;
    float4x4 cascadeViewProj[4];
    uint baseColorLayerCount;
    uint pad0;
    uint2 padBeforeLighting;
    ForwardLighting lighting;
    ShadowCascadeData cascades[4];
    uint screenWidth;
    uint screenHeight;
    uint debugViewMode;
};

// Must match RenderViewMode in core/render_view_mode.h
static const uint kViewFinal = 0u;
static const uint kViewAlbedo = 1u;
static const uint kViewAmbient = 2u;
static const uint kViewDiffuse = 3u;
static const uint kViewSpecular = 4u;
static const uint kViewPointLights = 5u;
static const uint kViewSSGI = 6u;
static const uint kViewShadow = 7u;
static const uint kViewNormal = 8u;
static const uint kViewDepth = 9u;
static const uint kViewNdotL = 10u;
static const uint kViewUV = 11u;
static const uint kViewMaterial = 12u;
static const uint kViewDirect = 13u;
static const uint kViewLighting = 14u;

ParameterBlock<ForwardParams> gFwd;

uniform Texture2DArray<float4> baseColorArray;
SamplerState linearSampler;
uniform Texture2D<float>.Handle shadowMap0;
uniform Texture2D<float>.Handle shadowMap1;
uniform Texture2D<float>.Handle shadowMap2;
uniform Texture2D<float>.Handle shadowMap3;
uniform SamplerComparisonState.Handle shadowSampler;
uniform StructuredBuffer<PointLightData>.Handle pointLights;
uniform Texture2D<float4>.Handle ssgiTexture;
uniform SamplerState.Handle ssgiSampler;

struct VSOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float material : MATERIAL;
    float viewDepth : TEXCOORD1;
    float4 shadowPos0 : TEXCOORD2;
    float4 shadowPos1 : TEXCOORD3;
    float4 shadowPos2 : TEXCOORD4;
    float4 shadowPos3 : TEXCOORD5;
    float3 worldPos : TEXCOORD6;
};

float3 safeNormalize(float3 v)
{
    return v * rsqrt(max(dot(v, v), 1e-8));
}

[shader("vertex")]
VSOut vertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL)
{
    float3 d = position - gFwd.camera.cameraPos;
    float vx = dot(d, gFwd.camera.right);
    float vy = dot(d, gFwd.camera.up);
    float vz = dot(d, gFwd.camera.forward);

    VSOut output;
    output.position = float4(vx * gFwd.camera.proj.x, vy * gFwd.camera.proj.y, vz - gFwd.camera.zParams.x, vz);
    output.normal = safeNormalize(normal);
    output.uv = uv;
    output.material = material;
    output.viewDepth = vz;
    output.worldPos = position;

    output.shadowPos0 = mul(gFwd.cascadeViewProj[0], float4(position, 1.0));
    output.shadowPos1 = mul(gFwd.cascadeViewProj[1], float4(position, 1.0));
    output.shadowPos2 = mul(gFwd.cascadeViewProj[2], float4(position, 1.0));
    output.shadowPos3 = mul(gFwd.cascadeViewProj[3], float4(position, 1.0));
    return output;
}

float sampleDirShadowMapCmp(int cascadeIndex, float2 uv, float compareDepth)
{
    switch (cascadeIndex)
    {
    case 0: return shadowMap0.SampleCmpLevelZero(shadowSampler, uv, compareDepth);
    case 1: return shadowMap1.SampleCmpLevelZero(shadowSampler, uv, compareDepth);
    case 2: return shadowMap2.SampleCmpLevelZero(shadowSampler, uv, compareDepth);
    default: return shadowMap3.SampleCmpLevelZero(shadowSampler, uv, compareDepth);
    }
}

float computeDirShadowPCF(float4 shadowClip, float texelSize, int cascadeIndex, float3 normal, float3 lightDir)
{
    float3 ndc = shadowClip.xyz / max(shadowClip.w, 1e-5);
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;
    float2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;

    float ndotl = saturate(dot(normal, lightDir));
    float bias = 0.0004 + 0.0025 * (1.0 - ndotl) + texelSize * 1.5;

    float shadow = 0.0;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2((float)x, (float)y) * texelSize;
            shadow += sampleDirShadowMapCmp(cascadeIndex, uv + offset, ndc.z - bias);
        }
    }
    return shadow / 9.0;
}

float evaluateDirShadow(float4 shadowPos[4], float viewDepth, float3 normal, float3 lightDir)
{
    int ci = 0;
    for (int i = 0; i < 4; i++)
    {
        if (viewDepth <= gFwd.cascades[i].splitFar)
        {
            ci = i;
            break;
        }
        ci = 3;
    }

    float4 clip = shadowPos[ci];
    float3 ndc = clip.xyz / max(clip.w, 1e-5);
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
    {
        for (int j = 0; j < 4; j++)
        {
            float3 altNdc = shadowPos[j].xyz / max(shadowPos[j].w, 1e-5);
            if (altNdc.x >= -1.0 && altNdc.x <= 1.0 && altNdc.y >= -1.0 && altNdc.y <= 1.0 &&
                altNdc.z >= 0.0 && altNdc.z <= 1.0)
            {
                ci = j;
                clip = shadowPos[j];
                break;
            }
        }
    }

    return computeDirShadowPCF(clip, gFwd.cascades[ci].texelSize, ci, normal, lightDir);
}

float3 evalPointLights(float3 albedo, float3 n, float3 worldPos)
{
    float3 sum = float3(0.0, 0.0, 0.0);
    if (gFwd.features.enablePointLights == 0u)
        return sum;

    uint count = gFwd.features.pointLightCount;
    for (uint i = 0; i < count; i++)
    {
        PointLightData l = pointLights[i];
        float3 toLight = l.position - worldPos;
        float distSq = dot(toLight, toLight);
        float radiusSq = l.radius * l.radius;
        if (distSq >= radiusSq)
            continue;

        float dist = sqrt(distSq);
        float3 ldir = toLight / max(dist, 1e-3);
        float atten = 1.0 - dist / l.radius;
        atten = atten * atten;
        float ndlPoint = saturate(dot(n, ldir));
        sum += albedo * l.color * ndlPoint * atten;
    }
    return sum;
}

float3 sampleSSGI(float4 pixelPos)
{
    float2 ssgiUV = pixelPos.xy / float2((float)gFwd.screenWidth, (float)gFwd.screenHeight);
    return max(ssgiTexture.SampleLevel(ssgiSampler, ssgiUV, 0).rgb, 0.0);
}

[shader("fragment")]
float4 fragmentMain(VSOut input) : SV_Target
{
    float3 n = safeNormalize(input.normal);

    float maxLayer = max((float)gFwd.baseColorLayerCount, 1.0) - 1.0;
    float layer = clamp(round(input.material), 0.0, maxLayer);
    float2 sampleUv = float2(frac(input.uv.x), 1.0 - frac(input.uv.y));
    float3 albedo = baseColorArray.SampleLevel(linearSampler, float3(sampleUv, layer), 0).rgb;
    albedo = max(albedo, 0.001);

    float3 viewDir = safeNormalize(gFwd.camera.cameraPos - input.worldPos);

    float3 ambientTerm = albedo * gFwd.lighting.ambientColor;

    float3 lightDir = safeNormalize(-gFwd.lighting.dirLightDir);
    float ndl = saturate(dot(n, lightDir));
    float dirShadow = 1.0;
    if (gFwd.features.enableDirShadow != 0u)
    {
        float4 shadowPos[4] = { input.shadowPos0, input.shadowPos1, input.shadowPos2, input.shadowPos3 };
        dirShadow = evaluateDirShadow(shadowPos, input.viewDepth, n, lightDir);
    }

    float3 diffuseTerm = float3(0.0, 0.0, 0.0);
    float3 specularTerm = float3(0.0, 0.0, 0.0);
    if (gFwd.features.enableDirLight != 0u)
    {
        diffuseTerm = albedo * gFwd.lighting.dirLightColor * gFwd.lighting.dirLightIntensity * ndl * dirShadow;
        float3 halfDir = safeNormalize(lightDir + viewDir);
        float spec = pow(saturate(dot(n, halfDir)), 48.0) * dirShadow;
        specularTerm = gFwd.lighting.dirLightColor * gFwd.lighting.dirLightIntensity * spec;
    }

    float3 pointTerm = evalPointLights(albedo, n, input.worldPos);

    float3 giTerm = float3(0.0, 0.0, 0.0);
    if (gFwd.features.enableSSGI != 0u)
        giTerm = sampleSSGI(input.position) * 0.25;

    float3 color = ambientTerm + diffuseTerm + pointTerm + giTerm;

    if (gFwd.features.enableFog != 0u)
    {
        float fog = saturate(input.viewDepth / 2600.0);
        color = lerp(color, float3(0.055, 0.065, 0.085), fog);
    }

    color = max(color, albedo * 0.22);

    uint mode = gFwd.debugViewMode;
    if (mode == kViewAlbedo)
        color = albedo;
    else if (mode == kViewAmbient)
        color = ambientTerm;
    else if (mode == kViewDiffuse)
        color = diffuseTerm;
    else if (mode == kViewSpecular)
        color = specularTerm;
    else if (mode == kViewPointLights)
        color = pointTerm;
    else if (mode == kViewSSGI)
        color = giTerm;
    else if (mode == kViewShadow)
        color = float3(dirShadow, dirShadow, dirShadow);
    else if (mode == kViewNormal)
        color = n * 0.5 + 0.5;
    else if (mode == kViewDepth)
    {
        float d = saturate(input.viewDepth / gFwd.camera.zParams.y);
        color = float3(d, d, d);
    }
    else if (mode == kViewNdotL)
        color = float3(ndl, ndl, ndl);
    else if (mode == kViewUV)
    {
        float2 uvv = frac(input.uv);
        color = float3(uvv.x, uvv.y, frac(uvv.x * 0.37 + uvv.y * 0.61));
    }
    else if (mode == kViewMaterial)
    {
        float t = layer / max(maxLayer, 1.0);
        color = float3(frac(t), frac(t * 2.13), frac(t * 4.71));
    }
    else if (mode == kViewDirect)
        color = max(diffuseTerm + pointTerm, albedo * 0.22);
    else if (mode == kViewLighting)
        color = max(ambientTerm + diffuseTerm + pointTerm, albedo * 0.22);

    return float4(color, 1.0);
}
