#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/shader/gpu_slang_module.h"
#include "gpu/core/gpu_internal.h"
#include <slang.h>
#include <string>
#include <vector>

GpuResult gpuCreateShaderCompiler(GpuDevice device, GpuShaderCompiler* outCompiler)
{
    if (!device || !outCompiler) return GPU_ERROR_INVALID_ARGS;
    GpuShaderCompiler compiler = new GpuShaderCompiler_t();
    compiler->device = device;
    *outCompiler = compiler;
    return GPU_SUCCESS;
}

GpuResult gpuCompileShader(GpuShaderCompiler compiler, const GpuShaderCompileDesc* desc, GpuShaderProgram* outProgram)
{
    if (!compiler || !desc || !outProgram) return GPU_ERROR_INVALID_ARGS;

    compiler->lastDiagnostic.clear();

    rhi::ComPtr<slang::ISession> slangSession;
    if (SLANG_FAILED(compiler->device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
        compiler->lastDiagnostic = "Failed to get Slang session";
        return GPU_ERROR_INTERNAL;
    }

    rhi::ComPtr<slang::IModule> modulePtr;
    slang::IBlob* diagnosticsBlob = nullptr;
    modulePtr = slangSession->loadModule(desc->sourcePath, &diagnosticsBlob);
    if (diagnosticsBlob) {
        compiler->lastDiagnostic = std::string(
            static_cast<const char*>(diagnosticsBlob->getBufferPointer()),
            diagnosticsBlob->getBufferSize());
        diagnosticsBlob->release();
    }
    if (!modulePtr) {
        if (compiler->lastDiagnostic.empty())
            compiler->lastDiagnostic = std::string("Failed to load module: ") + desc->sourcePath;
        return GPU_ERROR_INTERNAL;
    }

    rhi::ComPtr<slang::IEntryPoint> entryPoint;
    if (SLANG_FAILED(modulePtr->findEntryPointByName(desc->entryPoint, entryPoint.writeRef()))) {
        compiler->lastDiagnostic = std::string("Entry point not found: ") + desc->entryPoint;
        return GPU_ERROR_INVALID_ARGS;
    }

    // Optional: find fragment entry point for graphics pipelines
    rhi::ComPtr<slang::IEntryPoint> fragmentEntryPoint;
    if (desc->fragmentEntryPoint) {
        if (SLANG_FAILED(modulePtr->findEntryPointByName(desc->fragmentEntryPoint, fragmentEntryPoint.writeRef()))) {
            compiler->lastDiagnostic = std::string("Fragment entry point not found: ") + desc->fragmentEntryPoint;
            return GPU_ERROR_INVALID_ARGS;
        }
    }

    // Build entry point list
    slang::IComponentType* entryPoints[2] = {entryPoint.get(), nullptr};
    uint32_t entryPointCount = 1;
    if (fragmentEntryPoint) {
        entryPoints[1] = fragmentEntryPoint.get();
        entryPointCount = 2;
    }

    rhi::ShaderProgramDesc programDesc = {};
    programDesc.slangGlobalScope = modulePtr.get();
    programDesc.slangEntryPoints = entryPoints;
    programDesc.slangEntryPointCount = entryPointCount;

    rhi::ComPtr<rhi::IShaderProgram> rhiProgram;
    if (SLANG_FAILED(compiler->device->rhiDevice->createShaderProgram(programDesc, rhiProgram.writeRef()))) {
        compiler->lastDiagnostic = "Failed to create shader program";
        return GPU_ERROR_INTERNAL;
    }

    GpuShaderProgram program = new GpuShaderProgram_t();
    program->rhiProgram = rhiProgram;
    // Note: we don't store the compiled data since we're using the RHI program directly

    *outProgram = program;
    return GPU_SUCCESS;
}

void gpuDestroyShaderProgram(GpuShaderProgram program)
{
    if (!program) return;
    delete program;
}

void gpuDestroyShaderCompiler(GpuShaderCompiler compiler)
{
    if (!compiler) return;
    delete compiler;
}

const char* gpuGetShaderCompileDiagnostic(GpuShaderCompiler compiler)
{
    if (!compiler) return nullptr;
    return compiler->lastDiagnostic.empty() ? nullptr : compiler->lastDiagnostic.c_str();
}

const uint8_t* gpuGetShaderProgramData(GpuShaderProgram program, uint64_t* outSize)
{
    if (!program) {
        if (outSize) *outSize = 0;
        return nullptr;
    }
    if (outSize) *outSize = program->compiledData.size();
    return program->compiledData.empty() ? nullptr : program->compiledData.data();
}
