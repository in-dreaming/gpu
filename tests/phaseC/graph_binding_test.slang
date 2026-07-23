RWStructuredBuffer<uint> gOutput : register(u0);
struct GraphParameters { uint baseValue; };
ConstantBuffer<GraphParameters> gParameters : register(b1);

[numthreads(1, 1, 1)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gOutput[dispatchThreadId.x] = dispatchThreadId.x + gParameters.baseValue;
}
