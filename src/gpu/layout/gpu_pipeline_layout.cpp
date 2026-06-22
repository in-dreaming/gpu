#include "gpu/layout/gpu_pipeline_layout.h"
#include "gpu/core/gpu_internal.h"
#include <slang.h>
#include <string>
#include <vector>
#include <map>
#include <list>
#include <cstring>
#include <sstream>
#include <fstream>
#include <algorithm>

// ============================================================================
// Internal structures
// ============================================================================

struct GpuPipelineLayout_t {
    uint64_t layoutHash;
    std::vector<GpuBindingRange> bindingRanges;
    std::vector<GpuDescriptorSetLayoutInfo> descriptorSets;
    std::vector<GpuPushConstantRange> pushConstantRanges;
    std::vector<GpuEntryPointInfo> entryPoints;
    std::list<std::string> ownedStrings;  // list doesn't invalidate on insert
    uint32_t globalConstantBufferSize;
    int32_t bindlessSpaceIndex;

    // Per-set binding lists (for descriptorSets[i].bindings pointers)
    std::vector<std::vector<GpuBindingRange>> setBindings;
};

// ============================================================================
// Helpers
// ============================================================================

static const char* copyStringInternal(std::list<std::string>& storage, const char* src)
{
    if (!src) return nullptr;
    storage.push_back(std::string(src));
    return storage.back().c_str();
}

static GpuBindingKind slangBindingTypeToGpu(slang::BindingType type)
{
    uint32_t base = (uint32_t)type & 0xFF;
    switch ((slang::BindingType)base) {
    case slang::BindingType::Sampler:                        return GPU_BINDING_KIND_SAMPLER;
    case slang::BindingType::Texture:                        return GPU_BINDING_KIND_TEXTURE;
    case slang::BindingType::ConstantBuffer:                 return GPU_BINDING_KIND_CONSTANT_BUFFER;
    case slang::BindingType::ParameterBlock:                 return GPU_BINDING_KIND_PARAMETER_BLOCK;
    case slang::BindingType::TypedBuffer:                    return GPU_BINDING_KIND_TYPED_BUFFER;
    case slang::BindingType::RawBuffer:                      return GPU_BINDING_KIND_RAW_BUFFER;
    case slang::BindingType::CombinedTextureSampler:         return GPU_BINDING_KIND_COMBINED_TEXTURE_SAMPLER;
    case slang::BindingType::InputRenderTarget:              return GPU_BINDING_KIND_INPUT_RENDER_TARGET;
    case slang::BindingType::InlineUniformData:              return GPU_BINDING_KIND_INLINE_UNIFORM_DATA;
    case slang::BindingType::RayTracingAccelerationStructure:return GPU_BINDING_KIND_RAY_TRACING_ACCELERATION_STRUCTURE;
    case slang::BindingType::VaryingInput:                   return GPU_BINDING_KIND_VARYING_INPUT;
    case slang::BindingType::VaryingOutput:                  return GPU_BINDING_KIND_VARYING_OUTPUT;
    case slang::BindingType::ExistentialValue:               return GPU_BINDING_KIND_EXISTENTIAL_VALUE;
    case slang::BindingType::PushConstant:                   return GPU_BINDING_KIND_PUSH_CONSTANT;
    default:                                                 return GPU_BINDING_KIND_UNKNOWN;
    }
}

static GpuShaderStage slangStageToGpu(SlangStage stage)
{
    switch (stage) {
    case SLANG_STAGE_VERTEX:         return GPU_STAGE_VERTEX;
    case SLANG_STAGE_HULL:           return GPU_STAGE_HULL;
    case SLANG_STAGE_DOMAIN:         return GPU_STAGE_DOMAIN;
    case SLANG_STAGE_GEOMETRY:       return GPU_STAGE_GEOMETRY;
    case SLANG_STAGE_FRAGMENT:       return GPU_STAGE_FRAGMENT;
    case SLANG_STAGE_COMPUTE:        return GPU_STAGE_COMPUTE;
    case SLANG_STAGE_RAY_GENERATION: return GPU_STAGE_RAYGEN;
    case SLANG_STAGE_INTERSECTION:   return GPU_STAGE_INTERSECTION;
    case SLANG_STAGE_ANY_HIT:        return GPU_STAGE_ANY_HIT;
    case SLANG_STAGE_CLOSEST_HIT:    return GPU_STAGE_CLOSEST_HIT;
    case SLANG_STAGE_MISS:           return GPU_STAGE_MISS;
    case SLANG_STAGE_CALLABLE:       return GPU_STAGE_CALLABLE;
    case SLANG_STAGE_MESH:           return GPU_STAGE_MESH;
    case SLANG_STAGE_AMPLIFICATION:  return GPU_STAGE_AMPLIFICATION;
    default:                         return GPU_STAGE_NONE;
    }
}

static GpuShaderStageFlags stageToFlags(GpuShaderStage stage)
{
    if (stage == GPU_STAGE_NONE) return GPU_SHADER_STAGE_NONE;
    return (GpuShaderStageFlags)(1u << ((uint32_t)stage - 1u));
}

// ============================================================================
// Reflection: extract binding ranges from a TypeLayoutReflection
// ============================================================================

struct BindingRangeAccumulator {
    std::vector<GpuBindingRange> ranges;
    std::list<std::string>* stringStorage;
    GpuShaderStageFlags currentStageFlags;

    void extractFromTypeLayout(slang::TypeLayoutReflection* typeLayout)
    {
        if (!typeLayout) return;

        SlangInt rangeCount = typeLayout->getBindingRangeCount();
        for (SlangInt i = 0; i < rangeCount; i++) {
            slang::BindingType bt = typeLayout->getBindingRangeType(i);
            GpuBindingKind kind = slangBindingTypeToGpu(bt);

            // Skip varying input/output and existential values - not descriptor bindings
            if (kind == GPU_BINDING_KIND_VARYING_INPUT ||
                kind == GPU_BINDING_KIND_VARYING_OUTPUT ||
                kind == GPU_BINDING_KIND_EXISTENTIAL_VALUE ||
                kind == GPU_BINDING_KIND_UNKNOWN) {
                continue;
            }

            GpuBindingRange range = {};
            range.kind = kind;

            SlangInt count = typeLayout->getBindingRangeBindingCount(i);
            range.count = (count == SLANG_UNBOUNDED_SIZE || count == SLANG_UNKNOWN_SIZE)
                          ? UINT32_MAX : (uint32_t)count;

            // Get descriptor set index and binding index
            SlangInt descSetIndex = typeLayout->getBindingRangeDescriptorSetIndex(i);
            range.set = (descSetIndex >= 0) ? (uint32_t)descSetIndex : 0;

            // Get the leaf variable for name and binding index
            slang::VariableReflection* leafVar = typeLayout->getBindingRangeLeafVariable(i);
            if (leafVar) {
                const char* varName = leafVar->getName();
                range.name = copyStringInternal(*stringStorage, varName);
            } else {
                range.name = nullptr;
            }

            // Get the leaf type layout for binding index/space
            slang::TypeLayoutReflection* leafLayout = typeLayout->getBindingRangeLeafTypeLayout(i);
            if (leafLayout) {
                // Get descriptor set space offset
                if (descSetIndex >= 0 && descSetIndex < typeLayout->getDescriptorSetCount()) {
                    SlangInt spaceOffset = typeLayout->getDescriptorSetSpaceOffset(descSetIndex);
                    range.space = (spaceOffset >= 0) ? (uint32_t)spaceOffset : 0;
                }
            }

            // For binding index, we need to look at the variable layout
            // The binding range index within the descriptor set
            SlangInt descRangeCount = typeLayout->getBindingRangeDescriptorRangeCount(i);
            SlangInt firstDescRange = typeLayout->getBindingRangeFirstDescriptorRangeIndex(i);
            if (descRangeCount > 0 && firstDescRange >= 0 && descSetIndex >= 0) {
                // Use the first descriptor range's index offset as the binding index
                SlangInt indexOffset = typeLayout->getDescriptorSetDescriptorRangeIndexOffset(
                    descSetIndex, firstDescRange);
                range.binding = (indexOffset >= 0) ? (uint32_t)indexOffset : 0;
            }

            // Check for writable (UAV) flag
            bool isMutable = ((uint32_t)bt & (uint32_t)slang::BindingType::MutableFlag) != 0;
            range.writable = isMutable;

            range.stageFlags = currentStageFlags;
            ranges.push_back(range);
        }
    }
};

// ============================================================================
// Compute layout hash from binding ranges
// ============================================================================

static uint64_t computeLayoutHash(const std::vector<GpuBindingRange>& ranges,
                                   const std::vector<GpuPushConstantRange>& pushRanges,
                                   uint32_t entryPointCount,
                                   uint32_t globalCBSize)
{
    // Simple FNV-1a 64-bit hash
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    auto fnv = [&](const void* data, size_t size) {
        const uint8_t* p = (const uint8_t*)data;
        for (size_t i = 0; i < size; i++) {
            hash ^= (uint64_t)p[i];
            hash *= 1099511628211ULL; // FNV prime
        }
    };

    fnv(&globalCBSize, sizeof(globalCBSize));
    fnv(&entryPointCount, sizeof(entryPointCount));

    for (const auto& r : ranges) {
        fnv(&r.kind, sizeof(r.kind));
        fnv(&r.set, sizeof(r.set));
        fnv(&r.binding, sizeof(r.binding));
        fnv(&r.count, sizeof(r.count));
        fnv(&r.space, sizeof(r.space));
        fnv(&r.writable, sizeof(r.writable));
        if (r.name) {
            fnv(r.name, strlen(r.name));
        }
    }

    for (const auto& p : pushRanges) {
        fnv(&p.set, sizeof(p.set));
        fnv(&p.binding, sizeof(p.binding));
        fnv(&p.offset, sizeof(p.offset));
        fnv(&p.size, sizeof(p.size));
    }

    return hash;
}

// ============================================================================
// Main reflection function
// ============================================================================

GpuResult gpuReflectPipelineLayout(GpuShaderProgram program, GpuPipelineLayout* outLayout)
{
    if (!program || !outLayout) return GPU_ERROR_INVALID_ARGS;
    if (!program->linkedProgram) return GPU_ERROR_INVALID_ARGS;

    slang::ProgramLayout* programLayout = program->linkedProgram->getLayout();
    if (!programLayout) return GPU_ERROR_INTERNAL;

    GpuPipelineLayout layout = new GpuPipelineLayout_t();
    layout->globalConstantBufferSize = 0;
    layout->bindlessSpaceIndex = -1;

    // Extract global params binding ranges
    {
        slang::VariableLayoutReflection* globalParams = programLayout->getGlobalParamsVarLayout();
        if (globalParams) {
            slang::TypeLayoutReflection* globalTypeLayout = globalParams->getTypeLayout();
            if (globalTypeLayout) {
                layout->globalConstantBufferSize = (uint32_t)globalTypeLayout->getSize();

                BindingRangeAccumulator acc;
                acc.stringStorage = &layout->ownedStrings;
                acc.currentStageFlags = GPU_SHADER_STAGE_ALL;  // global params are visible to all stages
                acc.extractFromTypeLayout(globalTypeLayout);

                for (const auto& r : acc.ranges) {
                    layout->bindingRanges.push_back(r);
                }
            }
        }
    }

    // Extract entry point info and their binding ranges
    SlangUInt entryPointCount = programLayout->getEntryPointCount();
    for (SlangUInt epIdx = 0; epIdx < entryPointCount; epIdx++) {
        slang::EntryPointReflection* ep = programLayout->getEntryPointByIndex(epIdx);
        if (!ep) continue;

        GpuEntryPointInfo epInfo = {};
        const char* epName = ep->getName();
        epInfo.name = copyStringInternal(layout->ownedStrings, epName);
        epInfo.stage = slangStageToGpu(ep->getStage());
        epInfo.parameterCount = ep->getParameterCount();

        // Get compute thread group size if applicable
        if (epInfo.stage == GPU_STAGE_COMPUTE) {
            SlangUInt axisCount = 3;
            SlangUInt sizes[3] = {1, 1, 1};
            ep->getComputeThreadGroupSize(axisCount, sizes);
            epInfo.threadGroupSizeX = (uint32_t)sizes[0];
            epInfo.threadGroupSizeY = (uint32_t)sizes[1];
            epInfo.threadGroupSizeZ = (uint32_t)sizes[2];
        } else {
            epInfo.threadGroupSizeX = 0;
            epInfo.threadGroupSizeY = 0;
            epInfo.threadGroupSizeZ = 0;
        }

        layout->entryPoints.push_back(epInfo);

        // Extract binding ranges from entry point parameters
        GpuShaderStageFlags epStageFlags = stageToFlags(epInfo.stage);
        for (unsigned pIdx = 0; pIdx < ep->getParameterCount(); pIdx++) {
            slang::VariableLayoutReflection* param = ep->getParameterByIndex(pIdx);
            if (!param) continue;
            slang::TypeLayoutReflection* paramTypeLayout = param->getTypeLayout();
            if (!paramTypeLayout) continue;

            BindingRangeAccumulator acc;
            acc.stringStorage = &layout->ownedStrings;
            acc.currentStageFlags = epStageFlags;
            acc.extractFromTypeLayout(paramTypeLayout);

            for (const auto& r : acc.ranges) {
                // Merge stage flags if this binding already exists from global params
                bool found = false;
                for (auto& existing : layout->bindingRanges) {
                    if (existing.set == r.set && existing.binding == r.binding &&
                        existing.kind == r.kind && existing.space == r.space) {
                        existing.stageFlags |= r.stageFlags;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    layout->bindingRanges.push_back(r);
                }
            }
        }
    }

    // Get bindless space index
    SlangInt bindlessIdx = programLayout->getBindlessSpaceIndex();
    layout->bindlessSpaceIndex = (bindlessIdx >= 0) ? (int32_t)bindlessIdx : -1;

    // Extract push constant ranges (from binding ranges with kind == PUSH_CONSTANT)
    for (const auto& r : layout->bindingRanges) {
        if (r.kind == GPU_BINDING_KIND_PUSH_CONSTANT) {
            GpuPushConstantRange pcr = {};
            pcr.set = r.set;
            pcr.binding = r.binding;
            pcr.offset = 0;
            pcr.size = 0;  // TODO: extract actual size from reflection
            pcr.stageFlags = r.stageFlags;
            pcr.name = r.name;
            layout->pushConstantRanges.push_back(pcr);
        }
    }

    // Build descriptor set layouts by grouping binding ranges by set index
    std::map<uint32_t, std::vector<GpuBindingRange>> setsMap;
    for (const auto& r : layout->bindingRanges) {
        setsMap[r.set].push_back(r);
    }

    for (const auto& [setIdx, bindings] : setsMap) {
        GpuDescriptorSetLayoutInfo dsInfo = {};
        dsInfo.set = setIdx;
        dsInfo.space = bindings.empty() ? 0 : bindings[0].space;
        dsInfo.bindingCount = (uint32_t)bindings.size();
        layout->setBindings.push_back(bindings);
        dsInfo.bindings = nullptr;  // will be set below
        layout->descriptorSets.push_back(dsInfo);
    }

    // Now fix up bindings pointers to point into setBindings
    for (size_t i = 0; i < layout->descriptorSets.size(); i++) {
        layout->descriptorSets[i].bindings = layout->setBindings[i].data();
    }

    // Compute layout hash
    layout->layoutHash = computeLayoutHash(layout->bindingRanges,
                                            layout->pushConstantRanges,
                                            (uint32_t)layout->entryPoints.size(),
                                            layout->globalConstantBufferSize);

    *outLayout = layout;
    return GPU_SUCCESS;
}

GpuResult gpuGetPipelineLayoutInfo(GpuPipelineLayout layout, GpuPipelineLayoutInfo* outInfo)
{
    if (!layout || !outInfo) return GPU_ERROR_INVALID_ARGS;

    outInfo->layoutHash = layout->layoutHash;
    outInfo->descriptorSetCount = (uint32_t)layout->descriptorSets.size();
    outInfo->descriptorSets = layout->descriptorSets.data();
    outInfo->bindingRangeCount = (uint32_t)layout->bindingRanges.size();
    outInfo->bindingRanges = layout->bindingRanges.data();
    outInfo->pushConstantRangeCount = (uint32_t)layout->pushConstantRanges.size();
    outInfo->pushConstantRanges = layout->pushConstantRanges.data();
    outInfo->entryPointCount = (uint32_t)layout->entryPoints.size();
    outInfo->entryPoints = layout->entryPoints.data();
    outInfo->globalConstantBufferSize = layout->globalConstantBufferSize;
    outInfo->bindlessSpaceIndex = layout->bindlessSpaceIndex;

    return GPU_SUCCESS;
}

void gpuDestroyPipelineLayout(GpuPipelineLayout layout)
{
    if (!layout) return;
    delete layout;
}

uint64_t gpuGetPipelineLayoutHash(GpuPipelineLayout layout)
{
    if (!layout) return 0;
    return layout->layoutHash;
}

// ============================================================================
// JSON Serialization
// ============================================================================

static const char* bindingKindToString(GpuBindingKind kind)
{
    switch (kind) {
    case GPU_BINDING_KIND_SAMPLER:                         return "sampler";
    case GPU_BINDING_KIND_TEXTURE:                         return "texture";
    case GPU_BINDING_KIND_CONSTANT_BUFFER:                 return "constant_buffer";
    case GPU_BINDING_KIND_PARAMETER_BLOCK:                 return "parameter_block";
    case GPU_BINDING_KIND_TYPED_BUFFER:                    return "typed_buffer";
    case GPU_BINDING_KIND_RAW_BUFFER:                      return "raw_buffer";
    case GPU_BINDING_KIND_COMBINED_TEXTURE_SAMPLER:        return "combined_texture_sampler";
    case GPU_BINDING_KIND_INPUT_RENDER_TARGET:             return "input_render_target";
    case GPU_BINDING_KIND_INLINE_UNIFORM_DATA:             return "inline_uniform_data";
    case GPU_BINDING_KIND_RAY_TRACING_ACCELERATION_STRUCTURE: return "ray_tracing_acceleration_structure";
    case GPU_BINDING_KIND_VARYING_INPUT:                   return "varying_input";
    case GPU_BINDING_KIND_VARYING_OUTPUT:                  return "varying_output";
    case GPU_BINDING_KIND_EXISTENTIAL_VALUE:               return "existential_value";
    case GPU_BINDING_KIND_PUSH_CONSTANT:                   return "push_constant";
    default:                                               return "unknown";
    }
}

static const char* stageToString(GpuShaderStage stage)
{
    switch (stage) {
    case GPU_STAGE_VERTEX:    return "vertex";
    case GPU_STAGE_HULL:      return "hull";
    case GPU_STAGE_DOMAIN:    return "domain";
    case GPU_STAGE_GEOMETRY:  return "geometry";
    case GPU_STAGE_FRAGMENT:  return "fragment";
    case GPU_STAGE_COMPUTE:   return "compute";
    case GPU_STAGE_RAYGEN:    return "raygen";
    case GPU_STAGE_INTERSECTION: return "intersection";
    case GPU_STAGE_ANY_HIT:   return "any_hit";
    case GPU_STAGE_CLOSEST_HIT: return "closest_hit";
    case GPU_STAGE_MISS:      return "miss";
    case GPU_STAGE_CALLABLE:  return "callable";
    case GPU_STAGE_MESH:      return "mesh";
    case GPU_STAGE_AMPLIFICATION: return "amplification";
    default:                  return "none";
    }
}

static void writeJsonString(std::stringstream& ss, const char* str)
{
    if (!str) { ss << "null"; return; }
    ss << "\"";
    for (const char* p = str; *p; p++) {
        switch (*p) {
        case '"':  ss << "\\\""; break;
        case '\\': ss << "\\\\"; break;
        case '\n': ss << "\\n"; break;
        case '\r': ss << "\\r"; break;
        case '\t': ss << "\\t"; break;
        default:   ss << *p; break;
        }
    }
    ss << "\"";
}

static void writeIndent(std::stringstream& ss, int indent)
{
    for (int i = 0; i < indent; i++) ss << "  ";
}

static std::string buildLayoutJson(GpuPipelineLayout layout)
{
    std::stringstream ss;

    ss << "{\n";
    writeIndent(ss, 1); ss << "\"layoutHash\": \"" << std::hex << layout->layoutHash << std::dec << "\",\n";
    writeIndent(ss, 1); ss << "\"globalConstantBufferSize\": " << layout->globalConstantBufferSize << ",\n";
    writeIndent(ss, 1); ss << "\"bindlessSpaceIndex\": " << layout->bindlessSpaceIndex << ",\n";

    // Entry points
    writeIndent(ss, 1); ss << "\"entryPoints\": [\n";
    for (size_t i = 0; i < layout->entryPoints.size(); i++) {
        const auto& ep = layout->entryPoints[i];
        writeIndent(ss, 2); ss << "{\n";
        writeIndent(ss, 3); ss << "\"name\": "; writeJsonString(ss, ep.name); ss << ",\n";
        writeIndent(ss, 3); ss << "\"stage\": \"" << stageToString(ep.stage) << "\",\n";
        writeIndent(ss, 3); ss << "\"threadGroupSize\": [" << ep.threadGroupSizeX << ", " << ep.threadGroupSizeY << ", " << ep.threadGroupSizeZ << "],\n";
        writeIndent(ss, 3); ss << "\"parameterCount\": " << ep.parameterCount << "\n";
        writeIndent(ss, 2); ss << "}" << (i + 1 < layout->entryPoints.size() ? "," : "") << "\n";
    }
    writeIndent(ss, 1); ss << "],\n";

    // Binding ranges
    writeIndent(ss, 1); ss << "\"bindingRanges\": [\n";
    for (size_t i = 0; i < layout->bindingRanges.size(); i++) {
        const auto& r = layout->bindingRanges[i];
        writeIndent(ss, 2); ss << "{\n";
        writeIndent(ss, 3); ss << "\"kind\": \"" << bindingKindToString(r.kind) << "\",\n";
        writeIndent(ss, 3); ss << "\"set\": " << r.set << ",\n";
        writeIndent(ss, 3); ss << "\"binding\": " << r.binding << ",\n";
        writeIndent(ss, 3); ss << "\"count\": " << (r.count == UINT32_MAX ? -1 : (int)r.count) << ",\n";
        writeIndent(ss, 3); ss << "\"space\": " << r.space << ",\n";
        writeIndent(ss, 3); ss << "\"writable\": " << (r.writable ? "true" : "false") << ",\n";
        writeIndent(ss, 3); ss << "\"name\": "; writeJsonString(ss, r.name); ss << ",\n";
        writeIndent(ss, 3); ss << "\"stageFlags\": " << r.stageFlags << "\n";
        writeIndent(ss, 2); ss << "}" << (i + 1 < layout->bindingRanges.size() ? "," : "") << "\n";
    }
    writeIndent(ss, 1); ss << "],\n";

    // Descriptor sets
    writeIndent(ss, 1); ss << "\"descriptorSets\": [\n";
    for (size_t i = 0; i < layout->descriptorSets.size(); i++) {
        const auto& ds = layout->descriptorSets[i];
        writeIndent(ss, 2); ss << "{\n";
        writeIndent(ss, 3); ss << "\"set\": " << ds.set << ",\n";
        writeIndent(ss, 3); ss << "\"space\": " << ds.space << ",\n";
        writeIndent(ss, 3); ss << "\"bindingCount\": " << ds.bindingCount << "\n";
        writeIndent(ss, 2); ss << "}" << (i + 1 < layout->descriptorSets.size() ? "," : "") << "\n";
    }
    writeIndent(ss, 1); ss << "],\n";

    // Push constants
    writeIndent(ss, 1); ss << "\"pushConstants\": [\n";
    for (size_t i = 0; i < layout->pushConstantRanges.size(); i++) {
        const auto& pc = layout->pushConstantRanges[i];
        writeIndent(ss, 2); ss << "{\n";
        writeIndent(ss, 3); ss << "\"set\": " << pc.set << ",\n";
        writeIndent(ss, 3); ss << "\"binding\": " << pc.binding << ",\n";
        writeIndent(ss, 3); ss << "\"offset\": " << pc.offset << ",\n";
        writeIndent(ss, 3); ss << "\"size\": " << pc.size << ",\n";
        writeIndent(ss, 3); ss << "\"stageFlags\": " << pc.stageFlags << ",\n";
        writeIndent(ss, 3); ss << "\"name\": "; writeJsonString(ss, pc.name); ss << "\n";
        writeIndent(ss, 2); ss << "}" << (i + 1 < layout->pushConstantRanges.size() ? "," : "") << "\n";
    }
    writeIndent(ss, 1); ss << "]\n";

    ss << "}\n";
    return ss.str();
}

GpuResult gpuSerializePipelineLayoutJson(GpuPipelineLayout layout, const char* outputPath)
{
    if (!layout || !outputPath) return GPU_ERROR_INVALID_ARGS;

    std::string json = buildLayoutJson(layout);
    std::ofstream file(outputPath);
    if (!file.is_open()) return GPU_ERROR_INTERNAL;
    file << json;
    return GPU_SUCCESS;
}

GpuResult gpuGetPipelineLayoutJson(GpuPipelineLayout layout, char** outJson)
{
    if (!layout || !outJson) return GPU_ERROR_INVALID_ARGS;

    std::string json = buildLayoutJson(layout);
    char* buf = (char*)malloc(json.size() + 1);
    if (!buf) return GPU_ERROR_OUT_OF_MEMORY;
    memcpy(buf, json.c_str(), json.size() + 1);
    *outJson = buf;
    return GPU_SUCCESS;
}

void gpuFreePipelineLayoutJson(char* json)
{
    free(json);
}
