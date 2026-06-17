#include "gpu/shader/gpu_slang_module.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/core/gpu_internal.h"
#include <slang.h>
#include <string>
#include <vector>

struct GpuSlangModule_t {
    slang::IModule* module;
    std::vector<std::string> entryPointNames;
    std::vector<const char*> entryPointPtrs;
};

GpuResult gpuLoadSlangModule(GpuShaderCompiler compiler, const char* modulePath, GpuSlangModule* outModule)
{
    if (!compiler || !modulePath || !outModule) return GPU_ERROR_INVALID_ARGS;

    rhi::ComPtr<slang::ISession> slangSession;
    if (SLANG_FAILED(compiler->device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    slang::IBlob* diagnosticsBlob = nullptr;
    slang::IModule* module = slangSession->loadModule(modulePath, &diagnosticsBlob);
    if (!module) {
        if (diagnosticsBlob) diagnosticsBlob->release();
        return GPU_ERROR_INTERNAL;
    }
    if (diagnosticsBlob) diagnosticsBlob->release();

    GpuSlangModule mod = new GpuSlangModule_t();
    mod->module = module;

    uint32_t entryCount = module->getDefinedEntryPointCount();
    mod->entryPointNames.resize(entryCount);
    mod->entryPointPtrs.resize(entryCount);
    for (uint32_t i = 0; i < entryCount; i++) {
        slang::IEntryPoint* entry = nullptr;
        if (SLANG_SUCCEEDED(module->getDefinedEntryPoint(i, &entry))) {
            auto* reflection = entry->getFunctionReflection();
            if (reflection) {
                mod->entryPointNames[i] = reflection->getName();
            }
            if (mod->entryPointNames[i].empty()) {
                mod->entryPointNames[i] = "entry" + std::to_string(i);
            }
            entry->release();
        }
        mod->entryPointPtrs[i] = mod->entryPointNames[i].c_str();
    }

    *outModule = mod;
    return GPU_SUCCESS;
}

const char* gpuGetModuleEntryPoint(GpuSlangModule module, uint32_t index)
{
    if (!module || index >= module->entryPointPtrs.size()) return nullptr;
    return module->entryPointPtrs[index];
}

uint32_t gpuGetModuleEntryPointCount(GpuSlangModule module)
{
    if (!module) return 0;
    return (uint32_t)module->entryPointNames.size();
}

void gpuDestroySlangModule(GpuSlangModule module)
{
    if (!module) return;
    if (module->module) module->module->release();
    delete module;
}
