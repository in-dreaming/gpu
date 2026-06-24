#pragma once

#include "gpu/gpu.h"
#include "gpu/bindless/gpu_bindless_heap.h"
#include "gpu/core/gpu_internal.h"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4267)
#endif
#include <slang-rhi/shader-cursor.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cstdio>

inline bool shaderCursorSetBinding(rhi::ShaderCursor cursor, const char* field, rhi::ITextureView* view,
                                   const char* debugName = nullptr)
{
    if (!view) {
        if (debugName) printf("Binding failed: '%s' texture view is null\n", debugName);
        return false;
    }
    rhi::ShaderCursor fieldCursor = cursor[field];
    if (!fieldCursor.isValid()) {
        if (debugName) printf("Binding failed: shader field '%s' not found\n", debugName);
        return false;
    }
    if (SLANG_FAILED(fieldCursor.setBinding(view))) {
        if (debugName) printf("Binding failed: setBinding('%s') returned error\n", debugName);
        return false;
    }
    return true;
}

inline bool shaderCursorSetBinding(rhi::ShaderCursor cursor, const char* field, rhi::ISampler* sampler,
                                   const char* debugName = nullptr)
{
    if (!sampler) {
        if (debugName) printf("Binding failed: '%s' sampler is null\n", debugName);
        return false;
    }
    rhi::ShaderCursor fieldCursor = cursor[field];
    if (!fieldCursor.isValid()) {
        if (debugName) printf("Binding failed: shader field '%s' not found\n", debugName);
        return false;
    }
    if (SLANG_FAILED(fieldCursor.setBinding(sampler))) {
        if (debugName) printf("Binding failed: setBinding('%s') returned error\n", debugName);
        return false;
    }
    return true;
}

inline bool shaderCursorSetBindlessHeap(
    rhi::ShaderCursor cursor,
    const char* field,
    GpuBindlessHeap heap,
    uint32_t index,
    const char* debugName = nullptr)
{
    if (!cursor.isValid()) {
        if (debugName) printf("Bindless bind failed: cursor invalid for '%s'\n", debugName);
        return false;
    }
    if (!heap || index == UINT32_MAX) {
        if (debugName) printf("Bindless bind failed: '%s' heap/index invalid\n", debugName);
        return false;
    }
    GpuDescriptorHandleInfo info = {};
    if (gpuBindlessGetDescriptorHandle(heap, index, &info) != GPU_SUCCESS) {
        if (debugName) printf("Bindless bind failed: '%s' descriptor lookup failed\n", debugName);
        return false;
    }
    rhi::DescriptorHandle handle = {};
    handle.type = static_cast<rhi::DescriptorHandleType>(info.type);
    handle.value = info.value;
    if (handle.type == rhi::DescriptorHandleType::Undefined) {
        if (debugName) printf("Bindless bind failed: '%s' undefined descriptor type\n", debugName);
        return false;
    }
    rhi::ShaderCursor fieldCursor = cursor[field];
    if (!fieldCursor.isValid()) {
        if (debugName) printf("Bindless bind failed: shader field '%s' not found\n", debugName);
        return false;
    }
    if (SLANG_FAILED(fieldCursor.setDescriptorHandle(handle))) {
        if (debugName) printf("Bindless bind failed: setDescriptorHandle('%s') returned error\n", debugName);
        return false;
    }
    return true;
}

inline bool shaderCursorSetBindlessHeap(
    rhi::ShaderCursor cursor,
    GpuBindlessHeap heap,
    uint32_t index,
    const char* debugName = nullptr)
{
    if (!cursor.isValid()) {
        if (debugName) printf("Bindless bind failed: cursor invalid for '%s'\n", debugName);
        return false;
    }
    if (!heap || index == UINT32_MAX) {
        if (debugName) printf("Bindless bind failed: '%s' heap/index invalid\n", debugName);
        return false;
    }
    GpuDescriptorHandleInfo info = {};
    if (gpuBindlessGetDescriptorHandle(heap, index, &info) != GPU_SUCCESS) {
        if (debugName) printf("Bindless bind failed: '%s' descriptor lookup failed\n", debugName);
        return false;
    }
    rhi::DescriptorHandle handle = {};
    handle.type = static_cast<rhi::DescriptorHandleType>(info.type);
    handle.value = info.value;
    if (handle.type == rhi::DescriptorHandleType::Undefined) {
        if (debugName) printf("Bindless bind failed: '%s' undefined descriptor type\n", debugName);
        return false;
    }
    if (SLANG_FAILED(cursor.setDescriptorHandle(handle))) {
        if (debugName) printf("Bindless bind failed: setDescriptorHandle('%s') returned error\n", debugName);
        return false;
    }
    return true;
}
