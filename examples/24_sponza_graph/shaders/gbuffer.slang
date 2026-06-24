// G-buffer pass: albedo + world normal + depth for tiled cull / SSGI

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

CameraParams camera;
uint baseColorLayerCount;

uniform Texture2DArray<float4> baseColorArray;
SamplerState linearSampler;

struct VSOut
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float material : MATERIAL;
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
    return output;
}

struct GBufferOut
{
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
};

[shader("fragment")]
GBufferOut fragmentMain(VSOut input)
{
    float layer = clamp(round(input.material), 0.0, max((float)baseColorLayerCount, 1.0) - 1.0);
    float2 sampleUv = float2(frac(input.uv.x), 1.0 - frac(input.uv.y));
    float3 albedo = baseColorArray.SampleLevel(linearSampler, float3(sampleUv, layer), 0).rgb;

    GBufferOut output;
    output.albedo = float4(albedo, 1.0);
    output.normal = float4(input.normal * 0.5 + 0.5, 1.0);
    return output;
}
