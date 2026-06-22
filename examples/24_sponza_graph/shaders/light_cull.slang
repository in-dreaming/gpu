// Tile-based light culling compute shader
// Culls point lights per tile (16x16 pixels) for forward rendering

struct PointLightData
{
    float3 position;
    float radius;
    float3 color;
    float pad;
};

struct LightCullParams
{
    float4x4 projMatrix;
    float4x4 invProjMatrix;
    float4x4 viewMatrix;
    float4x4 invViewMatrix;
    uint screenWidth;
    uint screenHeight;
    uint lightCount;
    uint tileSize;
};

LightCullParams cullParams;
StructuredBuffer<PointLightData> pointLights;
RWStructuredBuffer<uint> lightIndexBuffer;
Texture2D<float> depthTexture;
SamplerState depthSampler;

groupshared uint visibleLightCount;
groupshared uint visibleLightIndices[128];

float4 getFrustumPlane(uint corner, float tileLeft, float tileTop, float tileRight, float tileBottom, float nearZ, float farZ)
{
    // Compute frustum planes for the tile
    float2 tileMin = float2(tileLeft, tileTop);
    float2 tileMax = float2(tileRight, tileBottom);

    float2 ndcCorner = float2(
        corner & 1 ? tileMax.x : tileMin.x,
        corner & 2 ? tileMax.y : tileMin.y
    ) / float2(cullParams.screenWidth, cullParams.screenHeight) * 2.0 - 1.0;
    ndcCorner.y = -ndcCorner.y;

    float nearDepth = nearZ;
    float4 nearPoint = mul(cullParams.invProjMatrix, float4(ndcCorner, nearDepth, 1.0));
    nearPoint /= nearPoint.w;

    float4 viewNear = mul(cullParams.invViewMatrix, nearPoint);

    float farDepth = farZ;
    float4 farPoint = mul(cullParams.invProjMatrix, float4(ndcCorner, farDepth, 1.0));
    farPoint /= farPoint.w;

    float4 viewFar = mul(cullParams.invViewMatrix, farPoint);

    float3 normal = normalize(cross(viewFar.xyz - viewNear.xyz, viewFar.xyz));
    return float4(normal, dot(normal, viewNear.xyz));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID, uint3 threadId : SV_DispatchThreadID)
{
    if (localId.x == 0 && localId.y == 0)
    {
        visibleLightCount = 0;
    }

    // Determine tile min/max depth
    uint2 tileMin = groupId.xy * 16;
    uint2 tileMax = min(tileMin + 16, uint2(cullParams.screenWidth, cullParams.screenHeight));

    float minDepth = 1.0;
    float maxDepth = 0.0;
    if (threadId.x < cullParams.screenWidth && threadId.y < cullParams.screenHeight)
    {
        float2 uv = (float2(threadId.xy) + 0.5) / float2(cullParams.screenWidth, cullParams.screenHeight);
        float depth = depthTexture[uint2(threadId.xy)].r;
        if (depth < minDepth) minDepth = depth;
        if (depth > maxDepth) maxDepth = depth;
    }

    // TODO: properly compute min/max depth per tile using groupshared
    // For simplicity, use fixed near/far
    float nearZ = 0.0;
    float farZ = 1.0;

    // Compute frustum planes for this tile
    float4 frustum[4];
    frustum[0] = getFrustumPlane(0, (float)tileMin.x, (float)tileMin.y, (float)tileMax.x, (float)tileMax.y, nearZ, farZ);
    frustum[1] = getFrustumPlane(1, (float)tileMin.x, (float)tileMin.y, (float)tileMax.x, (float)tileMax.y, nearZ, farZ);
    frustum[2] = getFrustumPlane(2, (float)tileMin.x, (float)tileMin.y, (float)tileMax.x, (float)tileMax.y, nearZ, farZ);
    frustum[3] = getFrustumPlane(3, (float)tileMin.x, (float)tileMin.y, (float)tileMax.x, (float)tileMax.y, nearZ, farZ);

    GroupMemoryBarrierWithGroupSync();

    // Cull lights for this tile
    for (uint i = localId.y * 16 + localId.x; i < cullParams.lightCount; i += 256)
    {
        PointLightData light = pointLights[i];
        float3 viewPos = mul(cullParams.viewMatrix, float4(light.position.x, light.position.y, light.position.z, 1.0)).xyz;
        float3 lightPos = light.position;

        // Check against frustum planes
        bool visible = true;
        for (int p = 0; p < 4 && visible; p++)
        {
            float d = dot(frustum[p].xyz, lightPos) - frustum[p].w;
            if (d < -light.radius) visible = false;
        }

        if (visible)
        {
            uint idx;
            InterlockedAdd(visibleLightCount, 1, idx);
            if (idx < 128) visibleLightIndices[idx] = i;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    // Write visible light indices to global buffer
    uint tileIndex = groupId.y * ((cullParams.screenWidth + 15) / 16) + groupId.x;
    uint base = tileIndex * 128;
    for (uint j = localId.y * 16 + localId.x; j < visibleLightCount; j += 256)
    {
        lightIndexBuffer[base + j] = visibleLightIndices[j];
    }

    if (localId.x == 0 && localId.y == 0)
    {
        lightIndexBuffer[base + 127] = visibleLightCount; // store count at last slot
    }
}
