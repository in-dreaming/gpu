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
    uint baseColorLayerCount;
    uint pad0;
    ForwardLighting lighting;
    ShadowCascadeData cascades[4];
    uint screenWidth;
    uint screenHeight;
    uint debugViewMode;
};

uint enableDirShadowFlag;
uint enableDirLightFlag;

StructuredBuffer<float4x4> cascadeViewProjs;
float4 gCascadeSplitFar;
float4 gCascadeTexelSize;
float4 gCascadeWorldTexel;
float4 gCascadeDepthBias;

ParameterBlock<ForwardParams> gFwd;
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
static const uint kViewShadowMap = 15u;
static const uint kViewShadowAtlas = 16u;
static const uint kViewShadowUv = 17u;

uniform Texture2DArray<float4> baseColorArray;
SamplerState linearSampler;
uniform Texture2D<float> shadowMap0;
uniform Texture2D<float> shadowMap1;
uniform Texture2D<float> shadowMap2;
uniform Texture2D<float> shadowMap3;
SamplerComparisonState shadowSampler;
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
    float3 worldPos : TEXCOORD2;
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
    return output;
}

static const uint kShadowMapResolution = 2048u;

float sampleShadowMapDepth(int cascadeIndex, int2 texel)
{
    texel = clamp(texel, int2(0, 0), int2((int)kShadowMapResolution - 1, (int)kShadowMapResolution - 1));
    switch (cascadeIndex)
    {
    case 0: return shadowMap0.Load(int3(texel, 0));
    case 1: return shadowMap1.Load(int3(texel, 0));
    case 2: return shadowMap2.Load(int3(texel, 0));
    default: return shadowMap3.Load(int3(texel, 0));
    }
}

float sampleShadowMapDepthUv(int cascadeIndex, float2 uv)
{
    int2 texel = int2(uv * (float)kShadowMapResolution);
    return sampleShadowMapDepth(cascadeIndex, texel);
}

float pickCascadeWorldTexel(int cascadeIndex)
{
    switch (cascadeIndex)
    {
    case 0: return gCascadeWorldTexel.x;
    case 1: return gCascadeWorldTexel.y;
    case 2: return gCascadeWorldTexel.z;
    default: return gCascadeWorldTexel.w;
    }
}

float pickCascadeDepthBiasNdc(int cascadeIndex)
{
    switch (cascadeIndex)
    {
    case 0: return gCascadeDepthBias.x;
    case 1: return gCascadeDepthBias.y;
    case 2: return gCascadeDepthBias.z;
    default: return gCascadeDepthBias.w;
    }
}

float compareShadowDepth(float mapDepth, float compareDepth, float bias)
{
    return (compareDepth > mapDepth - bias) ? 0.0 : 1.0;
}

float sampleShadowCmp(int cascadeIndex, float2 uv, float compareDepth, float bias)
{
    float mapDepth = sampleShadowMapDepthUv(cascadeIndex, uv);
    return compareShadowDepth(mapDepth, compareDepth, bias);
}

float computeDirShadowPCF(float4 shadowClip, float depthBiasNdc, int cascadeIndex, float3 normal, float3 lightDir)
{
    float ndotl = saturate(dot(normal, lightDir));
    if (ndotl < 0.01)
        return 1.0;

    float3 ndc = shadowClip.xyz / max(shadowClip.w, 1e-5);
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;
    float2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;

    float slopeScale = 1.0 - ndotl;
    float bias = depthBiasNdc * (1.0 + slopeScale * 2.0);
    return sampleShadowCmp(cascadeIndex, uv, ndc.z, bias);
}

float pickCascadeTexelSize(int cascadeIndex)
{
    switch (cascadeIndex)
    {
    case 0: return gCascadeTexelSize.x;
    case 1: return gCascadeTexelSize.y;
    case 2: return gCascadeTexelSize.z;
    default: return gCascadeTexelSize.w;
    }
}

float4 shadowClipFromWorld(float3 worldPos, int cascadeIndex)
{
    switch (cascadeIndex)
    {
    case 0: return mul(float4(worldPos, 1.0), cascadeViewProjs[0]);
    case 1: return mul(float4(worldPos, 1.0), cascadeViewProjs[1]);
    case 2: return mul(float4(worldPos, 1.0), cascadeViewProjs[2]);
    default: return mul(float4(worldPos, 1.0), cascadeViewProjs[3]);
    }
}

int pickCascadeIndex(float viewDepth)
{
    if (viewDepth <= gCascadeSplitFar.x)
        return 0;
    if (viewDepth <= gCascadeSplitFar.y)
        return 1;
    if (viewDepth <= gCascadeSplitFar.z)
        return 2;
    return 3;
}

float evaluateDirShadow(VSOut input, float3 n, float3 lightDir, float viewDepth, float materialLayer)
{
    if (materialLayer >= 0.5)
        return 1.0;

    float ndotl = saturate(dot(n, lightDir));
    if (ndotl < 0.01)
        return 1.0;

    int cascadeIndex = pickCascadeIndex(viewDepth);
    float depthBiasNdc = pickCascadeDepthBiasNdc(cascadeIndex);
    float3 samplePos = input.worldPos;
    float4 shadowClip = shadowClipFromWorld(samplePos, cascadeIndex);
    return computeDirShadowPCF(shadowClip, depthBiasNdc, cascadeIndex, n, lightDir);
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
    if (enableDirShadowFlag != 0u)
        dirShadow = evaluateDirShadow(input, n, lightDir, input.viewDepth, layer);

    float3 diffuseTerm = float3(0.0, 0.0, 0.0);
    float3 specularTerm = float3(0.0, 0.0, 0.0);
    if (enableDirLightFlag != 0u)
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

    color = max(color, albedo * 0.02);

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
    {
        if (enableDirShadowFlag == 0u)
            color = float3(1.0, 0.0, 1.0);
        else
            color = float3(dirShadow, dirShadow, dirShadow);
    }
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
        color = diffuseTerm + pointTerm;
    else if (mode == kViewLighting)
        color = ambientTerm + diffuseTerm + pointTerm;
    else if (mode == kViewShadowMap)
    {
        if (gFwd.features.enableDirShadow == 0u && enableDirShadowFlag == 0u)
        {
            color = float3(1.0, 0.0, 1.0);
        }
        else
        {
            int cascadeIndex = pickCascadeIndex(input.viewDepth);
            float4 shadowClip = shadowClipFromWorld(input.worldPos, cascadeIndex);
            float3 ndc = shadowClip.xyz / max(shadowClip.w, 1e-5);
            float2 uv = ndc.xy * 0.5 + 0.5;
            uv.y = 1.0 - uv.y;
            bool inBounds = ndc.x >= -1.0 && ndc.x <= 1.0 && ndc.y >= -1.0 && ndc.y <= 1.0 &&
                            ndc.z >= 0.0 && ndc.z <= 1.0;
            float mapDepth = 0.0;
            if (inBounds)
            {
                int2 texel = int2(uv * (float)kShadowMapResolution);
                mapDepth = sampleShadowMapDepth(cascadeIndex, texel);
            }
            color = inBounds ? float3(mapDepth, mapDepth, mapDepth) : float3(1.0, 0.0, 0.0);
        }
    }
    else if (mode == kViewShadowAtlas)
    {
        if (enableDirShadowFlag == 0u)
            color = float3(1.0, 0.0, 1.0);
        else
        {
            float2 atlasUv = input.position.xy / float2((float)gFwd.screenWidth, (float)gFwd.screenHeight);
            int2 texel = int2(atlasUv * 2048.0);
            float d = sampleShadowMapDepth(0, texel);
            color = float3(d, d, d);
        }
    }
    else if (mode == kViewShadowUv)
    {
        int cascadeIndex = pickCascadeIndex(input.viewDepth);
        float4 shadowClip = shadowClipFromWorld(input.worldPos, cascadeIndex);
        float3 ndc = shadowClip.xyz / max(shadowClip.w, 1e-5);
        float2 uv = ndc.xy * 0.5 + 0.5;
        uv.y = 1.0 - uv.y;
        bool inBounds = ndc.x >= -1.0 && ndc.x <= 1.0 && ndc.y >= -1.0 && ndc.y <= 1.0 &&
                        ndc.z >= 0.0 && ndc.z <= 1.0;
        color = inBounds ? float3(uv.x, uv.y, ndc.z) : float3(1.0, 0.0, 0.0);
    }

    return float4(color, 1.0);
}
