// Minimal compute shader used by async compute graph example.
RWStructuredBuffer<uint> gCounter : register(u0);

[shader("compute")]
[numthreads(1, 1, 1)]
void fillMain(uint3 tid : SV_DispatchThreadID)
{
    if (tid.x == 0u)
        gCounter[0] = 42u;
}
