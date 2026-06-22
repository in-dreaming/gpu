// Forward rendering shader with shadow mapping and multi-light support
// Features: directional light + point lights + shadow mapping + fog

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

struct ShadowCameraParams
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

struct PointLight
{
    float3 position;
    float radius;       // influence radius
    float3 color;
    float intensity;
};

struct LightUniforms
{
    ShadowCameraParams shadowCamera;
    float3 dirLightDir;
    float dirLightIntensity;
    float3 dirLightColor;
    float shadowTexelSize;
    float3 ambientColor;
    uint32_t pointLightCount;
    PointLight pointLights[8];
};

CameraParams camera;
LightUniforms lights;
Texture2DArray<float4> baseColorArray;
SamplerState linearSampler;
Texture2D<float> shadowMap;
SamplerComparisonState shadowSampler;

struct VSOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float material : MATERIAL;
    float viewDepth : TEXCOORD1;
    float4 shadowPos : TEXCOORD2;
    float3 worldPos : TEXCOORD3;
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

    // Compute shadow space position
    float3 sd = position - lights.shadowCamera.cameraPos;
    float svx = dot(sd, lights.shadowCamera.right);
    float svy = dot(sd, lights.shadowCamera.up);
    float svz = dot(sd, lights.shadowCamera.forward);
    output.shadowPos = float4(svx * lights.shadowCamera.proj.x, svy * lights.shadowCamera.proj.y, svz - lights.shadowCamera.zParams.x, svz);

    return output;
}

float computeShadow(float4 shadowPos, float3 worldPos)
{
    // Convert to NDC
    float3 ndc = shadowPos.xyz / shadowPos.w;
    // Only fragment in front of light far plane can be shadowed
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 || ndc.z > 1.0)
        return 1.0;  // outside shadow frustum — fully lit
    float2 uv = ndc.xy * 0.5 + 0.5;
    // Flip Y for Vulkan/D3D coordinate convention
    uv.y = 1.0 - uv.y;

    // PCF 3x3 shadow sampling
    float shadow = 0.0;
    float texel = lights.shadowTexelSize;
    float bias = 2.0;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 offset = float2((float)x, (float)y) * texel;
            shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, ndc.z - bias);
        }
    }
    shadow /= 9.0;
    return shadow;
}

[shader("fragment")]
float4 fragmentMain(VSOut input) : SV_Target
{
    float3 n = normalize(input.normal);
    float layer = clamp(round(input.material), 0.0, 4095.0);
    float3 albedo = baseColorArray.SampleLevel(linearSampler, float3(frac(input.uv), layer), 0).rgb;

    // Ambient
    float3 color = albedo * lights.ambientColor;

    // Directional light with shadow
    float3 lightDir = normalize(lights.dirLightDir);
    float ndl = saturate(dot(n, lightDir));
    float shadow = computeShadow(input.shadowPos, input.worldPos);
    color += albedo * lights.dirLightColor * lights.dirLightIntensity * ndl * shadow;

    // Point lights
    for (uint32_t i = 0; i < lights.pointLightCount; i++)
    {
        float3 toLight = lights.pointLights[i].position - input.worldPos;
        float dist = length(toLight);
        if (dist < lights.pointLights[i].radius)
        {
            float3 ldir = toLight / max(dist, 0.001);
            float atten = saturate(1.0 - dist / lights.pointLights[i].radius);
            atten = atten * atten;  // quadratic falloff
            float ndlPoint = saturate(dot(n, ldir));
            color += albedo * lights.pointLights[i].color * lights.pointLights[i].intensity * ndlPoint * atten;
        }
    }

    // Fog
    float fog = saturate(input.viewDepth / 2600.0);
    color = lerp(color, float3(0.055, 0.065, 0.085), fog);

    return float4(color, 1.0);
}
