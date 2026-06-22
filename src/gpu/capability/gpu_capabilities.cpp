#include "gpu/capability/gpu_capabilities.h"
#include "gpu/core/gpu_internal.h"
#include <cstring>

void gpuGetCapabilities(GpuDevice device, GpuCapabilities* outCaps)
{
    if (!device || !outCaps) return;

    const rhi::DeviceInfo& info = device->rhiDevice->getInfo();
    const rhi::DeviceLimits& limits = info.limits;

    memset(outCaps, 0, sizeof(GpuCapabilities));

    outCaps->maxTextureDimension = limits.maxTextureDimension2D;
    outCaps->minUniformBufferAlignment = limits.maxVertexStreamStride;

    outCaps->supportMeshShader = device->rhiDevice->hasFeature(rhi::Feature::MeshShader);
    outCaps->supportRayTracing = device->rhiDevice->hasFeature(rhi::Feature::RayTracing);
    outCaps->supportBindless = device->rhiDevice->hasFeature(rhi::Feature::Bindless);
    outCaps->supportWorkGraph = false;
    outCaps->supportCooperativeMatrix = device->rhiDevice->hasFeature(rhi::Feature::CooperativeMatrix);
    outCaps->supportSparseResource = false;

    outCaps->maxBindlessDescriptors = 1000000;

    // Phase E: Backend info
    outCaps->backendType = (uint32_t)info.deviceType;
    outCaps->timestampFrequency = info.timestampFrequency;
    if (info.apiName) {
#ifdef _MSC_VER
        strncpy_s(outCaps->apiName, info.apiName, sizeof(outCaps->apiName) - 1);
#else
        strncpy(outCaps->apiName, info.apiName, sizeof(outCaps->apiName) - 1);
        outCaps->apiName[sizeof(outCaps->apiName) - 1] = '\0';
#endif
    }
    if (info.adapterName) {
#ifdef _MSC_VER
        strncpy_s(outCaps->adapterName, info.adapterName, sizeof(outCaps->adapterName) - 1);
#else
        strncpy(outCaps->adapterName, info.adapterName, sizeof(outCaps->adapterName) - 1);
        outCaps->adapterName[sizeof(outCaps->adapterName) - 1] = '\0';
#endif
    }
}

bool gpuIsFeatureSupported(GpuDevice device, uint32_t feature)
{
    if (!device) return false;
    switch (feature) {
    case 0: return device->rhiDevice->hasFeature(rhi::Feature::MeshShader);
    case 1: return device->rhiDevice->hasFeature(rhi::Feature::RayTracing);
    case 2: return device->rhiDevice->hasFeature(rhi::Feature::Bindless);
    case 3: return device->rhiDevice->hasFeature(rhi::Feature::ParameterBlock);
    case 4: return device->rhiDevice->hasFeature(rhi::Feature::CooperativeMatrix);
    default: return false;
    }
}
