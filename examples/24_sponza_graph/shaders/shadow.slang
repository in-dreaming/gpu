// Shadow map shaders: orthographic CSM cascades + point-light cube faces

StructuredBuffer<float4x4> cascadeViewProjs;
uint shadowCascadeIndex;
float4x4 shadowViewProj;

[shader("vertex")]
float4 shadowVertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL,
    uint instanceId : SV_InstanceID) : SV_Position
{
    // CSM cascades: same row-vector multiply as forward.slang (must match depth sampling).
    if (shadowCascadeIndex < 4u)
        return mul(float4(position, 1.0), cascadeViewProjs[shadowCascadeIndex]);
    return mul(shadowViewProj, float4(position, 1.0));
}
