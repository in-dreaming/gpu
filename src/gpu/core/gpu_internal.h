#pragma once

#include <slang-rhi.h>
#include "gpu/core/gpu_format.h"

struct GpuDevice_t {
    rhi::ComPtr<rhi::IDevice> rhiDevice;
    rhi::ComPtr<rhi::ICommandQueue> graphicsQueue;
};

struct GpuCommandEncoder_t {
    rhi::ComPtr<rhi::ICommandEncoder> rhiEncoder;
    rhi::ICommandQueue* queue;
};

struct GpuCommandBuffer_t {
    rhi::ComPtr<rhi::ICommandBuffer> rhiCmdBuffer;
};

struct GpuSurface_t {
    rhi::ComPtr<rhi::ISurface> rhiSurface;
    uint32_t width;
    uint32_t height;
    GpuFormat format;
    bool configured;
};

struct GpuSurfaceTexture_t {
    rhi::ComPtr<rhi::ITexture> rhiTexture;
};
