// Screen-space global illumination from G-buffer color / normal / depth

static const uint kSSGISamples = 6;
static const uint kSSGIMaxSteps = 32;

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

struct SSGIParams
{
    CameraParams camera;
    float3 cameraPos;
    float pad0;
    float stepSize;
    float maxDistance;
    float thickness;
    uint screenWidth;
    uint screenHeight;
    uint fullWidth;
    uint fullHeight;
    uint temporalFrame;
};

ParameterBlock<SSGIParams> gSSGI;

uniform Texture2D<float4>.Handle albedoTexture;
uniform Texture2D<float4>.Handle normalTexture;
uniform Texture2D<float>.Handle depthTexture;
uniform SamplerState.Handle sceneSampler;
uniform RWTexture2D<float4>.Handle outputTexture;

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
    float traveled = gSSGI.stepSize;
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
        float sceneViewZ = viewDepthFromDeviceDepth(sceneDepth);
        if (rayViewZ >= sceneViewZ && (rayViewZ - sceneViewZ) <= gSSGI.thickness)
        {
            hitUv = uv;
            return true;
        }
    }
    return false;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadID.xy;
    if (pos.x >= gSSGI.screenWidth || pos.y >= gSSGI.screenHeight)
        return;

    float2 uv = (float2(pos) + 0.5) / float2((float)gSSGI.screenWidth, (float)gSSGI.screenHeight);
    float2 fullUv = (float2(pos) * 2.0 + 1.0) * 0.5 / float2((float)gSSGI.fullWidth, (float)gSSGI.fullHeight);

    float depth = depthTexture.SampleLevel(sceneSampler, fullUv, 0).r;
    if (depth >= 0.9999)
    {
        outputTexture[pos] = float4(0, 0, 0, 0);
        return;
    }

    float3 worldPos = worldPosFromUvDepth(fullUv, depth);
    float3 normal = normalize(normalTexture.SampleLevel(sceneSampler, fullUv, 0).rgb * 2.0 - 1.0);
    float3 albedo = albedoTexture.SampleLevel(sceneSampler, fullUv, 0).rgb;

    float3 indirect = 0;
    uint baseSeed = pos.x ^ (pos.y << 10) ^ (gSSGI.temporalFrame << 20);

    for (uint s = 0; s < kSSGISamples; s++)
    {
        float2 xi = hash22(uint2(baseSeed, s * 17u + 3u));
        float3 rayDir = cosineSampleHemisphere(normal, xi);
        float2 hitUv;
        if (!traceScreenSpaceRay(worldPos, rayDir, hitUv))
            continue;

        float3 hitAlbedo = albedoTexture.SampleLevel(sceneSampler, hitUv, 0).rgb;
        float hitDepth = depthTexture.SampleLevel(sceneSampler, hitUv, 0).r;
        float3 hitPos = worldPosFromUvDepth(hitUv, hitDepth);
        float3 hitNormal = normalize(normalTexture.SampleLevel(sceneSampler, hitUv, 0).rgb * 2.0 - 1.0);

        float3 toHit = normalize(hitPos - worldPos);
        float bounceWeight = saturate(dot(normal, toHit)) * saturate(dot(hitNormal, -toHit));
        indirect += hitAlbedo * bounceWeight;
    }

  indirect /= (float)kSSGISamples;
    indirect *= albedo;
    outputTexture[pos] = float4(indirect, 1.0);
}
