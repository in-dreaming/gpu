[[vk::binding(0, 0)]] Texture2D<float> gSource : register(t0, space0);
[[vk::binding(1, 0)]] RWTexture2D<float> gTarget : register(u1, space0);
struct TextureParameters { uint2 sourceExtent; uint2 targetExtent; };
[[vk::binding(2, 0)]] ConstantBuffer<TextureParameters> gParameters : register(b2, space0);

[numthreads(8, 8, 1)]
void computeMain(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= gParameters.targetExtent)) return;
    uint2 origin = id.xy * 2;
    uint2 maximum = gParameters.sourceExtent - 1;
    float result = 0.0;
    result = max(result, gSource.Load(int3(min(origin + uint2(0, 0), maximum), 0)));
    result = max(result, gSource.Load(int3(min(origin + uint2(1, 0), maximum), 0)));
    result = max(result, gSource.Load(int3(min(origin + uint2(0, 1), maximum), 0)));
    result = max(result, gSource.Load(int3(min(origin + uint2(1, 1), maximum), 0)));
    gTarget[id.xy] = result;
}
