#include "gpu/core/gpu_backend.h"
#include "gpu/core/gpu_internal.h"

GpuBackend gpuGetBackendType(GpuDevice device)
{
    if (!device) return GPU_BACKEND_DEFAULT;
    const rhi::DeviceInfo& info = device->rhiDevice->getInfo();
    switch (info.deviceType) {
    case rhi::DeviceType::D3D11:  return GPU_BACKEND_D3D11;
    case rhi::DeviceType::D3D12:  return GPU_BACKEND_D3D12;
    case rhi::DeviceType::Vulkan: return GPU_BACKEND_VULKAN;
    case rhi::DeviceType::Metal:  return GPU_BACKEND_METAL;
    case rhi::DeviceType::CPU:    return GPU_BACKEND_CPU;
    case rhi::DeviceType::CUDA:   return GPU_BACKEND_CUDA;
    case rhi::DeviceType::WGPU:   return GPU_BACKEND_WGPU;
    default:                      return GPU_BACKEND_DEFAULT;
    }
}

const char* gpuGetBackendName(GpuDevice device)
{
    if (!device) return nullptr;
    const rhi::DeviceInfo& info = device->rhiDevice->getInfo();
    return info.apiName;
}

const char* gpuGetAdapterName(GpuDevice device)
{
    if (!device) return nullptr;
    const rhi::DeviceInfo& info = device->rhiDevice->getInfo();
    return info.adapterName;
}

uint64_t gpuGetTimestampFrequency(GpuDevice device)
{
    if (!device) return 0;
    const rhi::DeviceInfo& info = device->rhiDevice->getInfo();
    return info.timestampFrequency;
}

const char* gpuBackendToString(GpuBackend backend)
{
    switch (backend) {
    case GPU_BACKEND_D3D11:  return "D3D11";
    case GPU_BACKEND_D3D12:  return "D3D12";
    case GPU_BACKEND_VULKAN: return "Vulkan";
    case GPU_BACKEND_METAL:  return "Metal";
    case GPU_BACKEND_CPU:    return "CPU";
    case GPU_BACKEND_CUDA:   return "CUDA";
    case GPU_BACKEND_WGPU:   return "WebGPU";
    case GPU_BACKEND_DEFAULT: return "Default";
    default:                  return "Unknown";
    }
}
