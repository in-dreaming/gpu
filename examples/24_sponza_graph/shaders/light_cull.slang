// Tiled light culling with per-tile depth range (clustered frustum slice)

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
    uint spotEnable;
    float3 direction;
    float innerConeCos;
    float outerConeCos;
};

struct LightCullParams
{
    uint screenWidth;
    uint screenHeight;
    uint lightCount;
    uint tileSize;
    CameraParams camera;
};

ParameterBlock<LightCullParams> gCull;

uniform StructuredBuffer<PointLightData>.Handle pointLights;
uniform RWStructuredBuffer<uint>.Handle lightIndexBuffer;
uniform Texture2D<float>.Handle depthTexture;
uniform SamplerState.Handle depthSampler;

groupshared uint gsMinDepthBits;
groupshared uint gsMaxDepthBits;
groupshared uint visibleLightCount;
groupshared uint visibleLightIndices[128];

float viewDepthFromDeviceDepth(float depth)
{
    float nearZ = gCull.camera.zParams.x;
    float farZ = gCull.camera.zParams.y;
    if (depth >= 0.9999)
        return farZ;
    return nearZ / max(1.0 - depth, 1e-5);
}

float3 viewPosFromPixelDepth(float2 pixel, float viewZ)
{
    float2 ndc = (pixel + 0.5) / float2((float)gCull.screenWidth, (float)gCull.screenHeight) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float vx = ndc.x * viewZ / gCull.camera.proj.x;
    float vy = ndc.y * viewZ / gCull.camera.proj.y;
    return float3(vx, vy, viewZ);
}

float3 lightViewPos(float3 worldPos)
{
    float3 d = worldPos - gCull.camera.cameraPos;
    return float3(
        dot(d, gCull.camera.right),
        dot(d, gCull.camera.up),
        dot(d, gCull.camera.forward));
}

void expandTileViewAabb(float2 tileMinPx, float2 tileMaxPx, float minZ, float maxZ, out float3 aabbMin, out float3 aabbMax)
{
    aabbMin = float3(1e9, 1e9, 1e9);
    aabbMax = float3(-1e9, -1e9, -1e9);

    float2 corners[4] = {
        tileMinPx,
        float2(tileMaxPx.x, tileMinPx.y),
        tileMaxPx,
        float2(tileMinPx.x, tileMaxPx.y)
    };

    for (int c = 0; c < 4; c++)
    {
        for (int zIdx = 0; zIdx < 2; zIdx++)
        {
            float z = (zIdx == 0) ? minZ : maxZ;
            float3 vp = viewPosFromPixelDepth(corners[c], z);
            aabbMin = min(aabbMin, vp);
            aabbMax = max(aabbMax, vp);
        }
    }
}

bool sphereIntersectsAabb(float3 center, float radius, float3 bmin, float3 bmax)
{
    float3 closest = clamp(center, bmin, bmax);
    float3 delta = center - closest;
    return dot(delta, delta) <= radius * radius;
}

bool lightIntersectsTileCluster(PointLightData light, float2 tileMinPx, float2 tileMaxPx, float tileMinZ, float tileMaxZ)
{
    float2 screenMax = float2((float)gCull.screenWidth, (float)gCull.screenHeight) - 1.0;
    tileMinPx = max(tileMinPx - 2.0, float2(0, 0));
    tileMaxPx = min(tileMaxPx + 2.0, screenMax);

    float3 lightView = lightViewPos(light.position);
    float nearZ = gCull.camera.zParams.x;

    if (lightView.z + light.radius < nearZ)
        return false;
    if (lightView.z - light.radius > tileMaxZ + 1.0)
        return false;

    float3 aabbMin, aabbMax;
    expandTileViewAabb(tileMinPx, tileMaxPx, tileMinZ, tileMaxZ, aabbMin, aabbMax);

    aabbMin -= light.radius;
    aabbMax += light.radius;
    return sphereIntersectsAabb(lightView, light.radius, aabbMin, aabbMax);
}

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 groupId : SV_GroupID, uint3 localId : SV_GroupThreadID, uint3 threadId : SV_DispatchThreadID)
{
    if (localId.x == 0 && localId.y == 0)
    {
        gsMinDepthBits = 0x7F7FFFFF;
        gsMaxDepthBits = 0;
        visibleLightCount = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    uint2 tileMin = groupId.xy * gCull.tileSize;
    uint2 tileMax = min(tileMin + gCull.tileSize, uint2(gCull.screenWidth, gCull.screenHeight));
    float2 tileMinPx = float2(tileMin);
    float2 tileMaxPx = float2(tileMax - uint2(1, 1));

    uint tileIndex = groupId.y * ((gCull.screenWidth + gCull.tileSize - 1) / gCull.tileSize) + groupId.x;
    uint base = tileIndex * 128;

    if (localId.x == 0 && localId.y == 0)
    {
        for (uint slot = 0; slot < 128; slot++)
            lightIndexBuffer[base + slot] = 0;
    }

    GroupMemoryBarrierWithGroupSync();

    if (threadId.x < gCull.screenWidth && threadId.y < gCull.screenHeight)
    {
        float2 uv = (float2(threadId.xy) + 0.5) / float2((float)gCull.screenWidth, (float)gCull.screenHeight);
        float depth = depthTexture.SampleLevel(depthSampler, uv, 0).r;
        if (depth < 0.9999)
        {
            float viewZ = viewDepthFromDeviceDepth(depth);
            uint zBits = asuint(viewZ);
            InterlockedMin(gsMinDepthBits, zBits);
            InterlockedMax(gsMaxDepthBits, zBits);
        }
    }

    GroupMemoryBarrierWithGroupSync();

    float tileMinZ = asfloat(gsMinDepthBits);
    float tileMaxZ = asfloat(gsMaxDepthBits);
    if (gsMaxDepthBits == 0)
    {
        tileMinZ = gCull.camera.zParams.x;
        tileMaxZ = gCull.camera.zParams.y;
    }
    else
    {
        tileMinZ = max(tileMinZ - 2.0, gCull.camera.zParams.x);
        tileMaxZ = min(tileMaxZ + 8.0, gCull.camera.zParams.y);
    }

    for (uint i = localId.y * 16 + localId.x; i < gCull.lightCount; i += 256)
    {
        PointLightData light = pointLights[i];
        if (lightIntersectsTileCluster(light, tileMinPx, tileMaxPx, tileMinZ, tileMaxZ))
        {
            uint idx;
            InterlockedAdd(visibleLightCount, 1, idx);
            if (idx < 127)
                visibleLightIndices[idx] = i;
        }
    }

    GroupMemoryBarrierWithGroupSync();

    uint cappedVisibleLightCount = min(visibleLightCount, 127u);
    for (uint j = localId.y * 16 + localId.x; j < cappedVisibleLightCount; j += 256)
        lightIndexBuffer[base + j] = visibleLightIndices[j];

    if (localId.x == 0 && localId.y == 0)
        lightIndexBuffer[base + 127] = cappedVisibleLightCount;
}
