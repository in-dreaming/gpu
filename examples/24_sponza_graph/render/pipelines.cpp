#include "pipelines.h"
#include "core/sponza_loader.h"
#include "render/render_resources.h"
#include "gpu/gpu.h"
#include "gpu/core/gpu_internal.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi.h>
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdio>

using namespace rhi;

bool createDemoPipelines(GpuDevice device, DemoPipelines& p, GpuFormat surfaceFormat) {
    if (gpuCreateShaderCompiler(device, &p.compiler) != GPU_SUCCESS) {
        printf("Compiler creation failed\n"); return false;
    }

    // Input layout
    InputElementDesc elems[] = {
        {"POSITION", 0, Format::RGB32Float, offsetof(SponzaVertex, position), 0},
        {"NORMAL", 0, Format::RGB32Float, offsetof(SponzaVertex, normal), 0},
        {"TEXCOORD", 0, Format::RG32Float, offsetof(SponzaVertex, uv), 0},
        {"MATERIAL", 0, Format::R32Float, offsetof(SponzaVertex, material), 0}
    };
    VertexStreamDesc stream = {sizeof(SponzaVertex), InputSlotClass::PerVertex, 0};
    InputLayoutDesc ild = {};
    ild.inputElements = elems;
    ild.inputElementCount = 4;
    ild.vertexStreams = &stream;
    ild.vertexStreamCount = 1;
    p.inputLayout = device->rhiDevice->createInputLayout(ild);
    if (!p.inputLayout) { printf("Input layout failed\n"); return false; }

    auto* rhiDevice = device->rhiDevice.get();

    // Shadow shader + pipeline
    {
        GpuShaderCompileDesc scd = {};
        scd.sourcePath = "shadow.slang";
        scd.entryPoint = "shadowVertexMain";
        scd.target = GPU_SHADER_TARGET_SPIRV;
        if (gpuCompileShader(p.compiler, &scd, &p.shadowProgram) != GPU_SUCCESS) {
            printf("Shadow shader failed: %s\n", gpuGetShaderCompileDiagnostic(p.compiler));
            return false;
        }
        p.shadowRoot = p.shadowProgram->rhiProgram;
        if (SLANG_FAILED(rhiDevice->createRootShaderObject(p.shadowRoot, p.shadowRootObj.writeRef()))) {
            printf("Shadow root object failed\n"); return false;
        }

        RenderPipelineDesc spd = {};
        spd.program = p.shadowRoot;
        spd.inputLayout = p.inputLayout;
        spd.targetCount = 0;
        spd.depthStencil.format = Format::D32Float;
        spd.depthStencil.depthTestEnable = true;
        spd.depthStencil.depthWriteEnable = true;
        spd.depthStencil.depthFunc = ComparisonFunc::Less;
        spd.rasterizer.cullMode = CullMode::None;
        spd.rasterizer.fillMode = FillMode::Solid;
        spd.label = "shadow_pipeline";
        if (SLANG_FAILED(rhiDevice->createRenderPipeline(spd, p.shadowPipeline.writeRef()))) {
            printf("Shadow pipeline failed\n"); return false;
        }
    }

    // Forward shader + pipeline
    {
        GpuShaderCompileDesc fcd = {};
        fcd.sourcePath = "forward.slang";
        fcd.entryPoint = "vertexMain";
        fcd.fragmentEntryPoint = "fragmentMain";
        fcd.target = GPU_SHADER_TARGET_SPIRV;
        if (gpuCompileShader(p.compiler, &fcd, &p.forwardProgram) != GPU_SUCCESS) {
            printf("Forward shader failed: %s\n", gpuGetShaderCompileDiagnostic(p.compiler));
            return false;
        }
        p.forwardRoot = p.forwardProgram->rhiProgram;
        if (SLANG_FAILED(rhiDevice->createRootShaderObject(p.forwardRoot, p.forwardRootObj.writeRef()))) {
            printf("Forward root object failed\n"); return false;
        }

        ColorTargetDesc tgt = {};
        tgt.format = (Format)gpuFormatToRhi(surfaceFormat);

        RenderPipelineDesc fpd = {};
        fpd.program = p.forwardRoot;
        fpd.inputLayout = p.inputLayout;
        fpd.targets = &tgt;
        fpd.targetCount = 1;
        fpd.depthStencil.format = Format::D32Float;
        fpd.depthStencil.depthTestEnable = true;
        fpd.depthStencil.depthWriteEnable = true;
        fpd.depthStencil.depthFunc = ComparisonFunc::Less;
        fpd.rasterizer.cullMode = CullMode::None;
        fpd.rasterizer.fillMode = FillMode::Solid;
        fpd.label = "forward_pipeline";
        if (SLANG_FAILED(rhiDevice->createRenderPipeline(fpd, p.forwardPipeline.writeRef()))) {
            printf("Forward pipeline failed\n"); return false;
        }
    }

    // SSGI compute shader + pipeline
    {
        GpuShaderCompileDesc scd = {};
        scd.sourcePath = "ssgi.slang";
        scd.entryPoint = "computeMain";
        scd.target = GPU_SHADER_TARGET_SPIRV;
        if (gpuCompileShader(p.compiler, &scd, &p.ssgiProgram) != GPU_SUCCESS) {
            printf("SSGI shader failed: %s\n", gpuGetShaderCompileDiagnostic(p.compiler));
            return false;
        }
        ComputePipelineDesc cpd = {};
        cpd.program = p.ssgiProgram->rhiProgram;
        cpd.label = "ssgi_pipeline";
        if (SLANG_FAILED(rhiDevice->createComputePipeline(cpd, p.ssgiPipeline.writeRef()))) {
            printf("SSGI pipeline failed\n"); return false;
        }
    }

    // Light cull compute shader + pipeline (optional)
    {
        GpuShaderCompileDesc lcd = {};
        lcd.sourcePath = "light_cull.slang";
        lcd.entryPoint = "computeMain";
        lcd.target = GPU_SHADER_TARGET_SPIRV;
        if (gpuCompileShader(p.compiler, &lcd, &p.lightCullProgram) != GPU_SUCCESS) {
            printf("Light cull shader compile failed (optional): %s\n", gpuGetShaderCompileDiagnostic(p.compiler));
        }
        if (p.lightCullProgram && p.lightCullProgram->rhiProgram) {
            ComputePipelineDesc cpd = {};
            cpd.program = p.lightCullProgram->rhiProgram;
            cpd.label = "light_cull_pipeline";
            if (SLANG_FAILED(rhiDevice->createComputePipeline(cpd, p.lightCullPipeline.writeRef()))) {
                printf("Light cull pipeline failed (optional)\n");
            }
        }
    }

    return true;
}

void destroyDemoPipelines(GpuDevice device, DemoPipelines& p) {
    (void)device;
    p.shadowPipeline.setNull();
    p.forwardPipeline.setNull();
    p.ssgiPipeline.setNull();
    p.lightCullPipeline.setNull();
    p.shadowRootObj.setNull();
    p.forwardRootObj.setNull();
    p.inputLayout.setNull();
    if (p.shadowProgram) { gpuDestroyShaderProgram(p.shadowProgram); p.shadowProgram = nullptr; }
    if (p.forwardProgram) { gpuDestroyShaderProgram(p.forwardProgram); p.forwardProgram = nullptr; }
    if (p.ssgiProgram) { gpuDestroyShaderProgram(p.ssgiProgram); p.ssgiProgram = nullptr; }
    if (p.lightCullProgram) { gpuDestroyShaderProgram(p.lightCullProgram); p.lightCullProgram = nullptr; }
    if (p.compiler) { gpuDestroyShaderCompiler(p.compiler); p.compiler = nullptr; }
}
