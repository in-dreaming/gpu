// Shadow map shader for demo 24
// Renders depth only from the light's perspective
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

ShadowCameraParams shadowCamera;

[shader("vertex")]
float4 shadowVertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL) : SV_Position
{
    float3 d = position - shadowCamera.cameraPos;
    float vx = dot(d, shadowCamera.right);
    float vy = dot(d, shadowCamera.up);
    float vz = dot(d, shadowCamera.forward);
    return float4(vx * shadowCamera.proj.x, vy * shadowCamera.proj.y, vz - shadowCamera.zParams.x, vz);
}
