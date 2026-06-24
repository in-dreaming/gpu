#include "gpu/reflection/gpu_reflection_cache.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include "gpu/core/gpu_internal.h"
#include <slang.h>
#include <string>
#include <map>
#include <mutex>

static GpuTypeKind slangTypeKindToGpu(slang::TypeReflection::Kind kind)
{
    switch (kind) {
    case slang::TypeReflection::Kind::Scalar:              return GPU_TYPE_KIND_SCALAR;
    case slang::TypeReflection::Kind::Vector:              return GPU_TYPE_KIND_VECTOR;
    case slang::TypeReflection::Kind::Matrix:              return GPU_TYPE_KIND_MATRIX;
    case slang::TypeReflection::Kind::Struct:              return GPU_TYPE_KIND_STRUCT;
    case slang::TypeReflection::Kind::Array:               return GPU_TYPE_KIND_ARRAY;
    case slang::TypeReflection::Kind::Resource:            return GPU_TYPE_KIND_TEXTURE;
    case slang::TypeReflection::Kind::SamplerState:        return GPU_TYPE_KIND_SAMPLER;
    case slang::TypeReflection::Kind::ShaderStorageBuffer: return GPU_TYPE_KIND_BUFFER;
    case slang::TypeReflection::Kind::ParameterBlock:      return GPU_TYPE_KIND_PARAMETER_BLOCK;
    case slang::TypeReflection::Kind::ConstantBuffer:      return GPU_TYPE_KIND_BUFFER;
    default:                                               return GPU_TYPE_KIND_SCALAR;
    }
}

static GpuTypeInfo* buildTypeInfo(slang::TypeLayoutReflection* typeLayout);

static char* copyStr(const char* src)
{
    if (!src) return nullptr;
    auto len = strlen(src);
    auto* dst = (char*)malloc(len + 1);
    memcpy(dst, src, len + 1);
    return dst;
}

static GpuTypeInfo* buildStructInfo(slang::TypeLayoutReflection* typeLayout)
{
    GpuTypeInfo* info = (GpuTypeInfo*)calloc(1, sizeof(GpuTypeInfo));
    info->kind = GPU_TYPE_KIND_STRUCT;

    auto* type = typeLayout->getType();
    if (type) {
        const char* name = type->getName();
        info->name = copyStr(name);
    }
    info->size = (uint32_t)typeLayout->getSize();
    info->alignment = 0;

    uint32_t fieldCount = typeLayout->getFieldCount();
    info->structInfo.fieldCount = fieldCount;
    info->structInfo.fields = (GpuStructField*)calloc(fieldCount, sizeof(GpuStructField));

    for (uint32_t i = 0; i < fieldCount; i++) {
        slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
        if (!field) continue;

        info->structInfo.fields[i].name = copyStr(field->getName());
        info->structInfo.fields[i].offset = (uint32_t)field->getOffset();
        info->structInfo.fields[i].type = buildTypeInfo(field->getTypeLayout());

        if (field->getBindingIndex() != 0 || field->getBindingSpace() != 0) {
            if (info->structInfo.fields[i].type) {
                info->structInfo.fields[i].type->bindingSlot = (uint32_t)field->getBindingIndex();
                info->structInfo.fields[i].type->bindingSpace = (uint32_t)field->getBindingSpace();
            }
        }
    }

    return info;
}

static GpuTypeInfo* buildVectorInfo(slang::TypeLayoutReflection* typeLayout)
{
    GpuTypeInfo* info = (GpuTypeInfo*)calloc(1, sizeof(GpuTypeInfo));
    info->kind = GPU_TYPE_KIND_VECTOR;
    info->size = (uint32_t)typeLayout->getSize();

    auto* elementType = typeLayout->getElementTypeLayout();
    if (elementType) {
        info->vector.count = (uint32_t)typeLayout->getElementCount();
        info->vector.scalarType = buildTypeInfo(elementType);
    }

    return info;
}

static GpuTypeInfo* buildMatrixInfo(slang::TypeLayoutReflection* typeLayout)
{
    GpuTypeInfo* info = (GpuTypeInfo*)calloc(1, sizeof(GpuTypeInfo));
    info->kind = GPU_TYPE_KIND_MATRIX;
    info->size = (uint32_t)typeLayout->getSize();

    auto* elementType = typeLayout->getElementTypeLayout();
    if (elementType) {
        info->matrix.rowCount = (uint32_t)typeLayout->getRowCount();
        info->matrix.colCount = (uint32_t)typeLayout->getColumnCount();
        info->matrix.scalarType = buildTypeInfo(elementType);
    }

    return info;
}

static GpuTypeInfo* buildArrayInfo(slang::TypeLayoutReflection* typeLayout)
{
    GpuTypeInfo* info = (GpuTypeInfo*)calloc(1, sizeof(GpuTypeInfo));
    info->kind = GPU_TYPE_KIND_ARRAY;
    info->size = (uint32_t)typeLayout->getSize();

    auto* elementType = typeLayout->getElementTypeLayout();
    if (elementType) {
        info->array.count = (uint32_t)typeLayout->getElementCount();
        info->array.element = buildTypeInfo(elementType);
    }

    return info;
}

static GpuTypeInfo* buildTypeInfo(slang::TypeLayoutReflection* typeLayout)
{
    if (!typeLayout) return nullptr;

    auto* type = typeLayout->getType();
    if (!type) return nullptr;

    auto kind = type->getKind();

    switch (kind) {
    case slang::TypeReflection::Kind::Struct:
        return buildStructInfo(typeLayout);
    case slang::TypeReflection::Kind::Vector:
        return buildVectorInfo(typeLayout);
    case slang::TypeReflection::Kind::Matrix:
        return buildMatrixInfo(typeLayout);
    case slang::TypeReflection::Kind::Array:
        return buildArrayInfo(typeLayout);
    case slang::TypeReflection::Kind::ParameterBlock: {
        GpuTypeInfo* info = (GpuTypeInfo*)calloc(1, sizeof(GpuTypeInfo));
        info->kind = GPU_TYPE_KIND_PARAMETER_BLOCK;
        info->size = (uint32_t)typeLayout->getSize();
        const char* name = type->getName();
        info->name = copyStr(name);
        auto* elementLayout = typeLayout->getElementTypeLayout();
        if (elementLayout) {
            info->array.element = buildTypeInfo(elementLayout);
            info->array.count = 1;
        }
        return info;
    }
    default: {
        GpuTypeInfo* info = (GpuTypeInfo*)calloc(1, sizeof(GpuTypeInfo));
        info->kind = slangTypeKindToGpu(kind);
        info->size = (uint32_t)typeLayout->getSize();
        const char* name = type->getName();
        info->name = copyStr(name);
        return info;
    }
    }
}

struct GpuReflectionCache_t {
    std::map<std::string, GpuTypeInfo*> cache;
    std::mutex mutex;
};

GpuResult gpuReflectionCacheCreate(GpuReflectionCache* outCache)
{
    if (!outCache) return GPU_ERROR_INVALID_ARGS;
    *outCache = new GpuReflectionCache_t();
    return GPU_SUCCESS;
}

GpuResult gpuReflectShader(GpuReflectionCache cache, GpuShaderCompiler compiler,
                           const char* modulePath, const char* entryPoint,
                           GpuTypeInfo** outTypeInfo)
{
    if (!cache || !compiler || !modulePath || !entryPoint || !outTypeInfo) return GPU_ERROR_INVALID_ARGS;

    std::string cacheKey = std::string(modulePath) + ":" + entryPoint;

    {
        std::lock_guard<std::mutex> lock(cache->mutex);
        auto it = cache->cache.find(cacheKey);
        if (it != cache->cache.end()) {
            *outTypeInfo = it->second;
            return GPU_SUCCESS;
        }
    }

    rhi::ComPtr<slang::ISession> slangSession;
    if (SLANG_FAILED(compiler->device->rhiDevice->getSlangSession(slangSession.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    rhi::ComPtr<slang::IModule> modulePtr;
    slang::IBlob* diagnosticsBlob = nullptr;
    modulePtr = slangSession->loadModule(modulePath, &diagnosticsBlob);
    if (diagnosticsBlob) diagnosticsBlob->release();
    if (!modulePtr) return GPU_ERROR_INTERNAL;

    rhi::ComPtr<slang::IEntryPoint> entryPt;
    if (SLANG_FAILED(modulePtr->findEntryPointByName(entryPoint, entryPt.writeRef()))) {
        return GPU_ERROR_INVALID_ARGS;
    }

    slang::IComponentType* components[] = {modulePtr.get(), entryPt.get()};
    rhi::ComPtr<slang::IComponentType> linked;
    rhi::ComPtr<slang::IBlob> linkDiag;
    if (SLANG_FAILED(slangSession->createCompositeComponentType(
        components, 2, linked.writeRef(), linkDiag.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    auto* reflection = linked->getLayout();
    if (!reflection) return GPU_ERROR_INTERNAL;

    GpuTypeInfo* typeInfo = buildTypeInfo(reflection->getGlobalParamsVarLayout()->getTypeLayout());

    {
        std::lock_guard<std::mutex> lock(cache->mutex);
        cache->cache[cacheKey] = typeInfo;
    }

    *outTypeInfo = typeInfo;
    return GPU_SUCCESS;
}

void gpuReflectionCacheDestroy(GpuReflectionCache cache)
{
    if (!cache) return;
    for (auto& [key, info] : cache->cache) {
        gpuTypeInfoDestroy(info);
    }
    delete cache;
}
