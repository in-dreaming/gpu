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
    int shadowMapIndex;
    uint spotEnable;
    float3 direction;
    float innerConeCos;
    float outerConeCos;
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
    uint screenWidth;
    uint screenHeight;
    uint lightCount;
    uint tileSize;
    CameraParams camera;
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
