// Shadow map shaders: orthographic CSM cascades + point-light cube faces

StructuredBuffer<float4x4> cascadeViewProjs;
uint shadowCascadeIndex;
float4x4 shadowViewProj;
float3 pointShadowLightPos;
float2 pointShadowNearFar;

[shader("vertex")]
float4 shadowVertexMain(
    float3 position : POSITION,
    float3 normal : NORMAL,
    float2 uv : TEXCOORD,
    float material : MATERIAL,
    uint instanceId : SV_InstanceID) : SV_Position
{
    if (shadowCascadeIndex < 4u)
        return mul(float4(position, 1.0), cascadeViewProjs[shadowCascadeIndex]);

    // Point cube faces: store linear radial depth in z/w for direction-based compare.
    float4 clip = mul(float4(position, 1.0), shadowViewProj);
    float dist = length(position - pointShadowLightPos);
    float depthNorm = (dist - pointShadowNearFar.x) / max(pointShadowNearFar.y - pointShadowNearFar.x, 1e-5);
    clip.z = saturate(depthNorm) * clip.w;
    return clip;
}
