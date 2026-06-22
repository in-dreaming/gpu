// Forward rendering shader with CSM (cascaded shadow maps), 1K+ point lights, SSGI blend
// Uses bindless for textures + structured buffer for lights

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
    float pad;
};

struct ShadowCascadeData
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float splitDepth;
    float texelSize;
    float2 pad;
};

CameraParams camera;
Texture2DArray<float4> baseColorArray;
SamplerState linearSampler;
Texture2D<float> shadowMaps[4];
SamplerComparisonState shadowSampler;
StructuredBuffer<PointLightData> pointLights;
uint pointLightCount;
float3 dirLightDir;
float dirLightIntensity;
float3 dirLightColor;
float3 ambientColor;
float4x4 cascadeViewProj[4];
ShadowCascadeData cascades[4];
Texture2D<float4> ssgiTexture;
SamplerState ssgiSampler;
uint screenWidth;
uint screenHeight;

struct VSOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float material : MATERIAL;
    float viewDepth : TEXCOORD1;
    float4 shadowPos[4] : TEXCOORD2;
    float3 worldPos : TEXCOORD6;
};

[shader("vertex")]
VSOut vertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL)
{
    float3 d = position - camera.cameraPos;
    float vx = dot(d, camera.right);
    float vy = dot(d, camera.up);
    float vz = dot(d, camera.forward);

    VSOut output;
    output.position = float4(vx * camera.proj.x, vy * camera.proj.y, vz - camera.zParams.x, vz);
    output.normal = normalize(normal);
    output.uv = uv;
    output.material = material;
    output.viewDepth = vz;
    output.worldPos = position;

    // Compute shadow positions for each cascade
    for (int i = 0; i < 4; i++)
    {
        float4 worldPos = float4(position, 1.0);
        float4 shadowClip = mul(cascadeViewProj[i], worldPos);
        output.shadowPos[i] = shadowClip;
    }

    return output;
}

float computeShadowCascade(float4 shadowPos, int cascadeIndex)
{
    // Convert to NDC
    float3 ndc = shadowPos.xyz / shadowPos.w;
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0; // outside cascade
    float2 uv = ndc.xy * 0.5 + 0.5;
    uv.y = 1.0 - uv.y; // flip Y

    float shadow = 0.0;
    float texel = cascades[cascadeIndex].texelSize;
    float bias = 0.001 + 0.001 * cascadeIndex;

    // PCF 3x3
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2((float)x, (float)y) * texel;
            shadow += shadowMaps[cascadeIndex].SampleCmpLevelZero(shadowSampler, uv + offset, ndc.z - bias);
        }
    }
    return shadow / 9.0;
}

[shader("fragment")]
float4 fragmentMain(VSOut input) : SV_Target
{
    float3 n = normalize(input.normal);
    float layer = clamp(round(input.material), 0.0, 4095.0);
    float3 albedo = baseColorArray.SampleLevel(linearSampler, float3(frac(input.uv), layer), 0).rgb;

    // Ambient
    float3 color = albedo * ambientColor;

    // Directional light with cascaded shadow
    float3 lightDir = normalize(dirLightDir);
    float ndl = saturate(dot(n, lightDir));

    // Find which cascade to use
    float shadow = 0.0;
    for (int ci = 0; ci < 4; ci++)
    {
        float3 ndc = input.shadowPos[ci].xyz / input.shadowPos[ci].w;
        if (ndc.x >= -1.0 && ndc.x <= 1.0 && ndc.y >= -1.0 && ndc.y <= 1.0 && ndc.z >= 0.0 && ndc.z <= 1.0)
        {
            shadow = computeShadowCascade(input.shadowPos[ci], ci);
            break;
        }
    }
    // If outside all cascades, fully lit
    if (shadow == 0.0) shadow = 1.0;

    color += albedo * dirLightColor * dirLightIntensity * ndl * shadow;

    // Point lights via structured buffer
    for (uint i = 0; i < pointLightCount; i++)
    {
        PointLightData l = pointLights[i];
        float3 toLight = float3(l.position.x, l.position.y, l.position.z) - input.worldPos;
        float dist = length(toLight);
        if (dist < l.radius)
        {
            float3 ldir = toLight / max(dist, 0.001);
            float atten = saturate(1.0 - dist / l.radius);
            atten = atten * atten;
            float ndlPoint = saturate(dot(n, ldir));
            color += albedo * float3(l.color.x, l.color.y, l.color.z) * ndlPoint * atten;
        }
    }

    // SSGI blend
    float2 ssgiUV = (float2(input.position.x, input.position.y) / float2((float)screenWidth, (float)screenHeight)) * 2.0;
    float3 indirect = ssgiTexture.SampleLevel(ssgiSampler, ssgiUV, 0).rgb;
    color += indirect * 0.5;

    // Fog
    float fog = saturate(input.viewDepth / 2600.0);
    color = lerp(color, float3(0.055, 0.065, 0.085), fog);

    return float4(color, 1.0);
}
