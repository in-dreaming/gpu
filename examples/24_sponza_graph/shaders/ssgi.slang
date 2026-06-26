// Screen-space GI: trace screen-space rays and sample lit surface radiance at hits.

static const uint kSSGISamples = 16;
static const uint kSSGIMaxSteps = 72;

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

struct SSGIParams
{
    CameraParams camera;
    float3 cameraPos;
    float pad0;
    float stepSize;
    float maxDistance;
    float thickness;
    float normalBias;
    float indirectBoost;
    uint screenWidth;
    uint screenHeight;
    uint fullWidth;
    uint fullHeight;
    uint temporalFrame;
    uint enablePointLights;
    uint pointLightCount;
    float2 pad1;
    float3 ambientColor;
    float pad2;
};

ParameterBlock<SSGIParams> gSSGI;

StructuredBuffer<PointLightData> pointLights;

Texture2D<float4> albedoTexture;
Texture2D<float4> normalTexture;
Texture2D<float> depthTexture;
SamplerState sceneSampler;
RWTexture2D<float4> outputTexture;

float hash11(uint n)
{
    n = (n << 13u) ^ n;
    return fract((float)(n * (n * n * 15731u + 789221u) + 1376312589u) / 4294967296.0);
}

float2 hash22(uint2 n)
{
    return float2(hash11(n.x ^ n.y), hash11(n.y ^ (n.x * 3u)));
}

float viewDepthFromDeviceDepth(float depth)
{
    float nearZ = gSSGI.camera.zParams.x;
    if (depth >= 0.9999)
        return gSSGI.camera.zParams.y;
    return nearZ / max(1.0 - depth, 1e-5);
}

float3 worldPosFromUvDepth(float2 uv, float depth)
{
    float viewZ = viewDepthFromDeviceDepth(depth);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float vx = ndc.x * viewZ / gSSGI.camera.proj.x;
    float vy = ndc.y * viewZ / gSSGI.camera.proj.y;
    return gSSGI.camera.cameraPos +
           gSSGI.camera.right * vx +
           gSSGI.camera.up * vy +
           gSSGI.camera.forward * viewZ;
}

float3 viewPosFromWorld(float3 worldPos)
{
    float3 d = worldPos - gSSGI.camera.cameraPos;
    return float3(
        dot(d, gSSGI.camera.right),
        dot(d, gSSGI.camera.up),
        dot(d, gSSGI.camera.forward));
}

bool projectToScreen(float3 worldPos, out float2 uv, out float viewZ)
{
    float3 viewPos = viewPosFromWorld(worldPos);
    viewZ = viewPos.z;
    if (viewZ <= gSSGI.camera.zParams.x)
        return false;
    float2 ndc = float2(viewPos.x * gSSGI.camera.proj.x, viewPos.y * gSSGI.camera.proj.y) / viewZ;
    uv = ndc * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    return all(uv >= 0.0) && all(uv <= 1.0);
}

float pointSpotAttenuation(PointLightData l, float3 toSurfaceNorm)
{
    if (l.spotEnable == 0u)
        return 1.0;
    float3 spotDir = normalize(l.direction);
    float cosTheta = dot(spotDir, toSurfaceNorm);
    return smoothstep(l.outerConeCos, l.innerConeCos, cosTheta);
}

float3 evalPointDiffuse(float3 albedo, float3 normal, float3 worldPos)
{
    float3 sum = 0;
    if (gSSGI.enablePointLights == 0u)
        return sum;

    uint count = gSSGI.pointLightCount;
    for (uint i = 0; i < count; i++)
    {
        PointLightData l = pointLights[i];
        float3 toLight = l.position - worldPos;
        float distSq = dot(toLight, toLight);
        float radiusSq = l.radius * l.radius;
        if (distSq >= radiusSq)
            continue;

        float dist = sqrt(distSq);
        float3 ldir = toLight / max(dist, 1e-3);
        float atten = 1.0 - dist / l.radius;
        atten = atten * atten;
        float spot = pointSpotAttenuation(l, -ldir);
        float ndl = saturate(dot(normal, ldir));
        sum += albedo * l.color * ndl * atten * spot;
    }
    return sum;
}

float3 evalSurfaceRadiance(float3 worldPos, float3 normal, float3 albedo)
{
    float3 radiance = albedo * gSSGI.ambientColor;
    radiance += evalPointDiffuse(albedo, normal, worldPos);
    return radiance;
}

bool sampleGbufferAt(float2 uv, out float3 worldPos, out float3 normal, out float3 albedo)
{
    float depth = depthTexture.SampleLevel(sceneSampler, uv, 0).r;
    if (depth >= 0.9999)
        return false;
    worldPos = worldPosFromUvDepth(uv, depth);
    normal = normalize(normalTexture.SampleLevel(sceneSampler, uv, 0).rgb * 2.0 - 1.0);
    albedo = albedoTexture.SampleLevel(sceneSampler, uv, 0).rgb;
    return true;
}

float3 cosineSampleHemisphere(float3 normal, float2 xi)
{
    float phi = 6.2831853 * xi.x;
    float cosTheta = sqrt(1.0 - xi.y);
    float sinTheta = sqrt(xi.y);
    float3 up = abs(normal.y) < 0.999 ? float3(0, 1, 0) : float3(1, 0, 0);
    float3 tangent = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);
    return normalize(tangent * cos(phi) * sinTheta + bitangent * sin(phi) * sinTheta + normal * cosTheta);
}

bool traceScreenSpaceRay(float3 origin, float3 dir, out float2 hitUv)
{
    float traveled = 0.0;
    float3 pos = origin;
    float2 uv = 0;
    float rayViewZ = 0;

    for (uint step = 0; step < kSSGIMaxSteps && traveled < gSSGI.maxDistance; step++)
    {
        pos += dir * gSSGI.stepSize;
        traveled += gSSGI.stepSize;
        if (!projectToScreen(pos, uv, rayViewZ))
            return false;

        float sceneDepth = depthTexture.SampleLevel(sceneSampler, uv, 0).r;
        if (sceneDepth >= 0.9999)
            continue;

        float sceneViewZ = viewDepthFromDeviceDepth(sceneDepth);
        if (rayViewZ >= sceneViewZ && (rayViewZ - sceneViewZ) <= gSSGI.thickness)
        {
            hitUv = uv;
            return true;
        }
    }
    return false;
}

float3 gatherScreenSpaceBleed(float2 uv, float depth, float3 normal, float3 directRad)
{
    float3 bleedSum = 0;
    float bleedW = 0;
    float centerViewZ = viewDepthFromDeviceDepth(depth);
    const int radius = 6;
    for (int y = -radius; y <= radius; y++)
    {
        for (int x = -radius; x <= radius; x++)
        {
            if (x == 0 && y == 0)
                continue;

            float2 nuv = uv + float2(x, y) / float2((float)gSSGI.fullWidth, (float)gSSGI.fullHeight) * 14.0;
            if (any(nuv < 0.0) || any(nuv > 1.0))
                continue;

            float3 nPos, nNormal, nAlbedo;
            if (!sampleGbufferAt(nuv, nPos, nNormal, nAlbedo))
                continue;

            float nDepth = depthTexture.SampleLevel(sceneSampler, nuv, 0).r;
            float nViewZ = viewDepthFromDeviceDepth(nDepth);
            float depthThreshold = max(2.5, centerViewZ * 0.14);
            if (abs(nViewZ - centerViewZ) > depthThreshold)
                continue;

            float dist2 = float(x * x + y * y) / float(radius * radius);
            float w = pow(saturate(dot(normal, nNormal)), 2.5) * exp(-dist2 * 1.6);
            float3 nRad = evalSurfaceRadiance(nPos, nNormal, nAlbedo);
            bleedSum += nRad * w;
            bleedW += w;
        }
    }
    if (bleedW <= 1e-4)
        return 0;
    float3 bleedAvg = bleedSum / bleedW;
    return max(bleedAvg - directRad * 0.55, 0.0);
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadID.xy;
    if (pos.x >= gSSGI.screenWidth || pos.y >= gSSGI.screenHeight)
        return;

    float2 uv = (float2(pos) + 0.5) / float2((float)gSSGI.screenWidth, (float)gSSGI.screenHeight);

    float3 worldPos, normal, albedo;
    if (!sampleGbufferAt(uv, worldPos, normal, albedo))
    {
        outputTexture[pos] = float4(0, 0, 0, 0);
        return;
    }

    float3 rayOrigin = worldPos + normal * gSSGI.normalBias;
    float3 directRad = evalSurfaceRadiance(worldPos, normal, albedo);
    float depth = depthTexture.SampleLevel(sceneSampler, uv, 0).r;
    float3 bleed = gatherScreenSpaceBleed(uv, depth, normal, directRad);

    float3 traced = 0;
    uint baseSeed = pos.x ^ (pos.y << 10) ^ (gSSGI.temporalFrame << 20);
    uint hitCount = 0;

    for (uint s = 0; s < kSSGISamples; s++)
    {
        float2 xi = hash22(uint2(baseSeed, s * 17u + 3u));
        float3 rayDir = cosineSampleHemisphere(normal, xi);
        float2 hitUv;
        if (!traceScreenSpaceRay(rayOrigin, rayDir, hitUv))
            continue;

        float3 hitPos, hitNormal, hitAlbedo;
        if (!sampleGbufferAt(hitUv, hitPos, hitNormal, hitAlbedo))
            continue;

        float3 hitRad = evalSurfaceRadiance(hitPos, hitNormal, hitAlbedo);
        float bounceWeight = saturate(dot(hitNormal, -rayDir)) * saturate(dot(normal, rayDir));
        traced += max(hitRad - directRad * 0.35, 0.0) * bounceWeight;
        hitCount++;
    }

    if (hitCount > 0u)
        traced /= (float)hitCount;

    float3 indirect = bleed + traced;
    indirect *= albedo * gSSGI.indirectBoost;
    outputTexture[pos] = float4(max(indirect, 0.0), 1.0);
}
