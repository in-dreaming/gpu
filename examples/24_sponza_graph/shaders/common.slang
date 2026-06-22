// Common shared types between shaders and C++ code

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
    float pad;
};

struct ShadowCascadeData
{
    float4x4 viewMatrix;
    float4x4 projMatrix;
    float splitDepth;
    float texelSize;
    float2 pad;
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

struct SSGIParams
{
    float4x4 projMatrix;
    float4x4 invProjMatrix;
    float4x4 viewMatrix;
    float4x4 invViewMatrix;
    float3 cameraPos;
    float stepSize;
    float maxDistance;
    uint screenWidth;
    uint screenHeight;
    uint temporalFrame;
};
