#pragma once

#include "gpu/gpu.h"
#include "core/sponza_loader.h"
#include <slang-rhi.h>

// All pipelines and shader objects for the demo
struct DemoPipelines {
    // C API programs
    GpuShaderProgram shadowProgram = nullptr;
    GpuShaderProgram gbufferProgram = nullptr;
    GpuShaderProgram forwardProgram = nullptr;
    GpuShaderProgram ssgiProgram = nullptr;
    GpuShaderProgram lightCullProgram = nullptr;

    // RHI-level objects (used for actual rendering)
    rhi::ComPtr<rhi::IRenderPipeline> shadowPipeline;
    rhi::ComPtr<rhi::IRenderPipeline> gbufferPipeline;
    rhi::ComPtr<rhi::IRenderPipeline> forwardPipeline;
    rhi::ComPtr<rhi::IComputePipeline> ssgiPipeline;
    rhi::ComPtr<rhi::IComputePipeline> lightCullPipeline;

    rhi::ComPtr<rhi::IInputLayout> inputLayout;
    rhi::ComPtr<rhi::IShaderProgram> shadowRoot;
    rhi::ComPtr<rhi::IShaderObject> shadowRootObj;
    rhi::ComPtr<rhi::IShaderProgram> gbufferRoot;
    rhi::ComPtr<rhi::IShaderProgram> forwardRoot;
    rhi::ComPtr<rhi::IShaderObject> gbufferRootObj;
    rhi::ComPtr<rhi::IShaderObject> forwardRootObj;
    rhi::ComPtr<rhi::IShaderObject> ssgiRootObj;
    rhi::ComPtr<rhi::IShaderObject> lightCullRootObj;

    GpuShaderCompiler compiler = nullptr;
};

bool createDemoPipelines(GpuDevice device, DemoPipelines& p, GpuFormat surfaceFormat);
void destroyDemoPipelines(GpuDevice device, DemoPipelines& p);
