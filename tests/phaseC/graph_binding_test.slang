RWStructuredBuffer<uint> gOutput : register(u0);

[numthreads(1, 1, 1)]
void computeMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gOutput[dispatchThreadId.x] = dispatchThreadId.x + 41;
}
