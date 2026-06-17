#include "gpu/reflection/gpu_meta_gen.h"
#include <fstream>
#include <sstream>
#include <string>

static void writeTypeScriptType(std::stringstream& ss, const GpuTypeInfo* info, int indent);

static void writeIndent(std::stringstream& ss, int indent)
{
    for (int i = 0; i < indent; i++) ss << "    ";
}

static void writeTypeScriptScalar(std::stringstream& ss, const char* name)
{
    if (!name) { ss << "number"; return; }
    std::string n = name;
    if (n == "float" || n == "double" || n == "half") { ss << "number"; return; }
    if (n == "int" || n == "uint" || n == "int64" || n == "uint64") { ss << "number"; return; }
    if (n == "bool") { ss << "boolean"; return; }
    ss << "number";
}

static void writeTypeScriptField(std::stringstream& ss, const GpuStructField* field, int indent)
{
    writeIndent(ss, indent);
    ss << field->name << ": ";
    writeTypeScriptType(ss, field->type, indent);
    ss << ";\n";
}

static void writeTypeScriptType(std::stringstream& ss, const GpuTypeInfo* info, int indent)
{
    if (!info) { ss << "unknown"; return; }

    switch (info->kind) {
    case GPU_TYPE_KIND_SCALAR:
        writeTypeScriptScalar(ss, info->name);
        break;
    case GPU_TYPE_KIND_VECTOR:
        if (info->vector.count == 3)
            ss << "[number, number, number]";
        else if (info->vector.count == 4)
            ss << "[number, number, number, number]";
        else if (info->vector.count == 2)
            ss << "[number, number]";
        else
            ss << "number[]";
        break;
    case GPU_TYPE_KIND_MATRIX:
        ss << "number[]";
        break;
    case GPU_TYPE_KIND_STRUCT:
        ss << "{\n";
        for (uint32_t i = 0; i < info->structInfo.fieldCount; i++) {
            writeTypeScriptField(ss, &info->structInfo.fields[i], indent + 1);
        }
        writeIndent(ss, indent);
        ss << "}";
        break;
    case GPU_TYPE_KIND_ARRAY:
        writeTypeScriptType(ss, info->array.element, indent);
        ss << "[]";
        break;
    case GPU_TYPE_KIND_TEXTURE:
        ss << "number";
        break;
    case GPU_TYPE_KIND_SAMPLER:
        ss << "number";
        break;
    case GPU_TYPE_KIND_BUFFER:
        ss << "ArrayBuffer";
        break;
    case GPU_TYPE_KIND_PARAMETER_BLOCK:
        ss << "object";
        break;
    default:
        ss << "unknown";
        break;
    }
}

GpuResult gpuGenerateTypeScript(GpuTypeInfo* typeInfo, const char* outputPath)
{
    if (!typeInfo || !outputPath) return GPU_ERROR_INVALID_ARGS;

    std::stringstream ss;
    ss << "// Auto-generated from Slang reflection\n\n";
    ss << "interface " << (typeInfo->name ? typeInfo->name : "Unknown") << " ";
    writeTypeScriptType(ss, typeInfo, 0);
    ss << "\n";

    std::ofstream file(outputPath);
    if (!file.is_open()) return GPU_ERROR_INTERNAL;
    file << ss.str();
    return GPU_SUCCESS;
}

static void writeJsonType(std::stringstream& ss, const GpuTypeInfo* info, int indent);

static void writeJsonIndent(std::stringstream& ss, int indent)
{
    for (int i = 0; i < indent; i++) ss << "  ";
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

static const char* kindToString(GpuTypeKind kind)
{
    switch (kind) {
    case GPU_TYPE_KIND_SCALAR:        return "scalar";
    case GPU_TYPE_KIND_VECTOR:        return "vector";
    case GPU_TYPE_KIND_MATRIX:        return "matrix";
    case GPU_TYPE_KIND_STRUCT:        return "struct";
    case GPU_TYPE_KIND_ARRAY:         return "array";
    case GPU_TYPE_KIND_TEXTURE:       return "texture";
    case GPU_TYPE_KIND_SAMPLER:       return "sampler";
    case GPU_TYPE_KIND_BUFFER:        return "buffer";
    case GPU_TYPE_KIND_PARAMETER_BLOCK: return "parameter_block";
    default:                          return "unknown";
    }
}

static void writeJsonType(std::stringstream& ss, const GpuTypeInfo* info, int indent)
{
    if (!info) { ss << "null"; return; }

    ss << "{\n";
    writeJsonIndent(ss, indent + 1);
    ss << "\"kind\": \"" << kindToString(info->kind) << "\",\n";
    writeJsonIndent(ss, indent + 1);
    ss << "\"name\": ";
    writeJsonString(ss, info->name);
    ss << ",\n";
    writeJsonIndent(ss, indent + 1);
    ss << "\"size\": " << info->size << ",\n";
    writeJsonIndent(ss, indent + 1);
    ss << "\"alignment\": " << info->alignment;

    switch (info->kind) {
    case GPU_TYPE_KIND_STRUCT:
        ss << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"fields\": [\n";
        for (uint32_t i = 0; i < info->structInfo.fieldCount; i++) {
            writeJsonIndent(ss, indent + 2);
            ss << "{\n";
            writeJsonIndent(ss, indent + 3);
            ss << "\"name\": ";
            writeJsonString(ss, info->structInfo.fields[i].name);
            ss << ",\n";
            writeJsonIndent(ss, indent + 3);
            ss << "\"offset\": " << info->structInfo.fields[i].offset << ",\n";
            writeJsonIndent(ss, indent + 3);
            ss << "\"type\": ";
            writeJsonType(ss, info->structInfo.fields[i].type, indent + 3);
            ss << "\n";
            writeJsonIndent(ss, indent + 2);
            ss << "}" << (i + 1 < info->structInfo.fieldCount ? "," : "") << "\n";
        }
        writeJsonIndent(ss, indent + 1);
        ss << "]";
        break;
    case GPU_TYPE_KIND_ARRAY:
        ss << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"count\": " << info->array.count << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"element\": ";
        writeJsonType(ss, info->array.element, indent + 1);
        break;
    case GPU_TYPE_KIND_VECTOR:
        ss << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"count\": " << info->vector.count << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"scalarType\": ";
        writeJsonType(ss, info->vector.scalarType, indent + 1);
        break;
    case GPU_TYPE_KIND_MATRIX:
        ss << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"rows\": " << info->matrix.rowCount << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"cols\": " << info->matrix.colCount << ",\n";
        writeJsonIndent(ss, indent + 1);
        ss << "\"scalarType\": ";
        writeJsonType(ss, info->matrix.scalarType, indent + 1);
        break;
    default:
        if (info->bindingSlot != 0 || info->bindingSpace != 0) {
            ss << ",\n";
            writeJsonIndent(ss, indent + 1);
            ss << "\"bindingSlot\": " << info->bindingSlot << ",\n";
            writeJsonIndent(ss, indent + 1);
            ss << "\"bindingSpace\": " << info->bindingSpace;
        }
        break;
    }

    ss << "\n";
    writeJsonIndent(ss, indent);
    ss << "}";
}

GpuResult gpuGenerateJSON(GpuTypeInfo* typeInfo, const char* outputPath)
{
    if (!typeInfo || !outputPath) return GPU_ERROR_INVALID_ARGS;

    std::stringstream ss;
    writeJsonType(ss, typeInfo, 0);
    ss << "\n";

    std::ofstream file(outputPath);
    if (!file.is_open()) return GPU_ERROR_INTERNAL;
    file << ss.str();
    return GPU_SUCCESS;
}
