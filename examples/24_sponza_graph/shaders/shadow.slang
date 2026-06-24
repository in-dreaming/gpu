// Shadow map shaders: orthographic CSM cascades + point-light cube faces

float4x4 shadowViewProj;

[shader("vertex")]
float4 shadowVertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL,
    uint instanceId : SV_InstanceID) : SV_Position
{
    return mul(shadowViewProj, float4(position, 1.0));
}
