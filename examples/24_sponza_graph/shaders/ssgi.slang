// Screen-space global illumination compute shader
// Simple ray march in screen-space to approximate indirect lighting

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
    float4x4 projMatrix;
    float4x4 invProjMatrix;
    float4x4 viewMatrix;
    float4x4 invViewMatrix;
    float3 cameraPos;
    float pad0;
    float stepSize;
    float maxDistance;
    uint screenWidth;
    uint screenHeight;
    uint temporalFrame;
};

CameraParams camera;
SSGIParams ssgiParams;
Texture2D<float> depthTexture;
SamplerState depthSampler;
RWTexture2D<float4> outputTexture;

float3 getWorldPos(float2 uv, float depth)
{
    float4 clipPos = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 worldPos = mul(ssgiParams.invViewMatrix, mul(ssgiParams.invProjMatrix, clipPos));
    return worldPos.xyz / worldPos.w;
}

float3 getNormal(float2 uv, float2 texelSize)
{
    float c = depthTexture.SampleLevel(depthSampler, uv, 0).r;
    float l = depthTexture.SampleLevel(depthSampler, uv + float2(-texelSize.x, 0), 0).r;
    float r = depthTexture.SampleLevel(depthSampler, uv + float2(texelSize.x, 0), 0).r;
    float d = depthTexture.SampleLevel(depthSampler, uv + float2(0, texelSize.y), 0).r;
    float u = depthTexture.SampleLevel(depthSampler, uv + float2(0, -texelSize.y), 0).r;

    float3 wp = getWorldPos(uv, c);
    float3 wl = getWorldPos(uv + float2(-texelSize.x, 0), l);
    float3 wr = getWorldPos(uv + float2(texelSize.x, 0), r);
    float3 wd = getWorldPos(uv + float2(0, texelSize.y), d);
    float3 wu = getWorldPos(uv + float2(0, -texelSize.y), u);

    float3 dx = wr - wl;
    float3 dy = wd - wu;
    return normalize(cross(dx, dy));
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    uint2 pos = dispatchThreadID.xy;
    if (pos.x >= ssgiParams.screenWidth || pos.y >= ssgiParams.screenHeight) return;

    float2 uv = (float2(pos) + 0.5) / float2(ssgiParams.screenWidth, ssgiParams.screenHeight);
    float depth = depthTexture.SampleLevel(depthSampler, uv, 0).r;

    if (depth >= 1.0)
    {
        outputTexture[pos] = float4(0, 0, 0, 0);
        return;
    }

    float3 worldPos = getWorldPos(uv, depth);
    float3 normal = getNormal(uv, float2(1.0 / ssgiParams.screenWidth, 1.0 / ssgiParams.screenHeight));
    float3 viewDir = normalize(ssgiParams.cameraPos - worldPos);

    // Random ray direction (hemisphere oriented by normal)
    uint seed = pos.x ^ (pos.y << 7) ^ (ssgiParams.temporalFrame << 13);
    float randAngle = (seed & 0xFFFF) / 65536.0 * 6.28318;
    float randCos = ((seed >> 16) & 0xFFFF) / 65536.0;
    float randSin = sqrt(1.0 - randCos * randCos);

    float3 tangent = normalize(cross(normal, abs(normal.y) < 0.99 ? float3(0, 1, 0) : float3(1, 0, 0)));
    float3 bitangent = cross(normal, tangent);
    float3 rayDir = normalize(tangent * cos(randAngle) * randSin + bitangent * sin(randAngle) * randSin + normal * randCos);

    // Ray march
    float3 rayPos = worldPos + normal * 0.1;
    float3 indirect = float3(0, 0, 0);
    int sampleCount = 0;

    for (float t = 0.1; t < ssgiParams.maxDistance; t += ssgiParams.stepSize)
    {
        float3 samplePos = rayPos + rayDir * t;
        float4 clipPos = mul(ssgiParams.projMatrix, float4(samplePos - camera.cameraPos, 1.0));
        // TODO: proper camera projection (simplified)
        float3 sampleUV = clipPos.xyz / clipPos.w * 0.5 + 0.5;
        if (sampleUV.x < 0 || sampleUV.x > 1 || sampleUV.y < 0 || sampleUV.y > 1) break;

        float sampleDepth = depthTexture.SampleLevel(depthSampler, sampleUV.xy, 0).r;
        float3 sampleWorld = getWorldPos(sampleUV.xy, sampleDepth);
        float distToSample = length(sampleWorld - samplePos);

        if (distToSample < ssgiParams.stepSize * 2.0)
        {
            indirect += float3(0.02, 0.03, 0.05); // simple ambient contribution
            sampleCount++;
        }
    }

    if (sampleCount > 0) indirect /= sampleCount;
    outputTexture[pos] = float4(indirect, 1.0);
}
