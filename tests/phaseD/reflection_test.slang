// Test shader for Phase D reflection tests
// Has various binding types for pipeline layout reflection

struct VSOutput {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

struct UniformParams {
    float4x4 mvp;
    float4 color;
    float time;
    float _pad0;
    float _pad1;
    float _pad2;
};

// Constant buffer at set 0, binding 0
uniform ConstantBuffer<UniformParams> gUniforms;

// Texture at set 0, binding 1
Texture2D gTexture;

// Sampler at set 0, binding 2
SamplerState gSampler;

// Structured buffer at set 0, binding 3
StructuredBuffer<float4> gVertices;

// RW structured buffer (UAV) at set 0, binding 4
RWStructuredBuffer<float4> gOutput;

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    VSOutput output;
    float2 positions[3] = {
        float2( 0.0,  0.5),
        float2( 0.5, -0.5),
        float2(-0.5, -0.5)
    };
    output.position = float4(positions[vertexID], 0.0, 1.0);
    output.uv = positions[vertexID] + 0.5;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    float4 texColor = gTexture.Sample(gSampler, input.uv);
    return texColor * gUniforms.color;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void computeMain(uint3 dispatchID : SV_DispatchThreadID) {
    if (dispatchID.x < 256 && dispatchID.y < 256) {
        uint idx = dispatchID.y * 256 + dispatchID.x;
        gOutput[idx] = gVertices[idx] * gUniforms.mvp[0];
    }
}
