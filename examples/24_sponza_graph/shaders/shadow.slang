// Shadow map shaders for CSM (cascaded shadow maps)
// Depth-only vertex shader for each cascade

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

struct CascadeUniforms
{
    ShadowCameraParams cascades[4];
};

CascadeUniforms cascadeData;

[shader("vertex")]
float4 shadowVertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL,
    uint instanceId : SV_InstanceID) : SV_Position
{
    ShadowCameraParams cam = cascadeData.cascades[instanceId];
    float3 d = position - cam.cameraPos;
    float vx = dot(d, cam.right);
    float vy = dot(d, cam.up);
    float vz = dot(d, cam.forward);
    return float4(vx * cam.proj.x, vy * cam.proj.y, vz - cam.zParams.x, vz);
}
