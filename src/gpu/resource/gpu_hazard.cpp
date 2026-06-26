#include "gpu/resource/gpu_hazard.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/debug/gpu_validation.h"

GpuAccessFlags gpuAccessFlagsForResourceState(GpuResourceState state)
{
    switch (state) {
    case GPU_RESOURCE_STATE_SHADER_RESOURCE:
    case GPU_RESOURCE_STATE_STORAGE_BUFFER:
        return GPU_ACCESS_SHADER_READ;
    case GPU_RESOURCE_STATE_UNORDERED_ACCESS:
        return GPU_ACCESS_SHADER_READ | GPU_ACCESS_SHADER_WRITE;
    case GPU_RESOURCE_STATE_RENDER_TARGET:
        return GPU_ACCESS_COLOR_ATTACHMENT | GPU_ACCESS_SHADER_WRITE;
    case GPU_RESOURCE_STATE_DEPTH_WRITE:
        return GPU_ACCESS_DEPTH_WRITE;
    case GPU_RESOURCE_STATE_DEPTH_READ:
        return GPU_ACCESS_DEPTH_READ | GPU_ACCESS_SHADER_READ;
    case GPU_RESOURCE_STATE_COPY_SOURCE:
        return GPU_ACCESS_COPY_READ;
    case GPU_RESOURCE_STATE_COPY_DEST:
        return GPU_ACCESS_COPY_WRITE;
    case GPU_RESOURCE_STATE_PRESENT:
        return GPU_ACCESS_PRESENT;
    default:
        return GPU_ACCESS_NONE;
    }
}

GpuResourceState gpuResourceStateForAccessFlags(GpuAccessFlags access, bool isBuffer, bool isDepthTexture)
{
    if (access & GPU_ACCESS_PRESENT) return GPU_RESOURCE_STATE_PRESENT;
    if (access & GPU_ACCESS_COLOR_ATTACHMENT) return GPU_RESOURCE_STATE_RENDER_TARGET;
    if (access & GPU_ACCESS_DEPTH_WRITE) return GPU_RESOURCE_STATE_DEPTH_WRITE;
    if (access & GPU_ACCESS_DEPTH_READ) return GPU_RESOURCE_STATE_DEPTH_READ;
    if (access & GPU_ACCESS_COPY_WRITE) return GPU_RESOURCE_STATE_COPY_DEST;
    if (access & GPU_ACCESS_COPY_READ) return GPU_RESOURCE_STATE_COPY_SOURCE;
    if (access & GPU_ACCESS_SHADER_WRITE) {
        return isBuffer ? GPU_RESOURCE_STATE_UNORDERED_ACCESS : GPU_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    if (access & GPU_ACCESS_SHADER_READ) {
        if (isDepthTexture) return GPU_RESOURCE_STATE_DEPTH_READ;
        return isBuffer ? GPU_RESOURCE_STATE_STORAGE_BUFFER : GPU_RESOURCE_STATE_SHADER_RESOURCE;
    }
    return GPU_RESOURCE_STATE_UNDEFINED;
}

bool gpuAccessFlagsIsWrite(GpuAccessFlags access)
{
    return (access & (GPU_ACCESS_SHADER_WRITE | GPU_ACCESS_COLOR_ATTACHMENT | GPU_ACCESS_DEPTH_WRITE |
                      GPU_ACCESS_COPY_WRITE | GPU_ACCESS_PRESENT)) != 0;
}

bool gpuResourceStateIsUav(GpuResourceState state)
{
    return state == GPU_RESOURCE_STATE_UNORDERED_ACCESS;
}

bool gpuHazardNeedsGlobalBarrier(GpuResourceState beforeState, GpuAccessFlags nextAccess)
{
    if (!gpuResourceStateIsUav(beforeState)) return false;
    return gpuAccessFlagsIsWrite(nextAccess) || (nextAccess & GPU_ACCESS_SHADER_READ) != 0;
}

GpuHazardKind gpuHazardClassify(GpuResourceState currentState, GpuAccessFlags nextAccess, bool hadWriter)
{
    if (currentState == GPU_RESOURCE_STATE_UNDEFINED)
        return GPU_HAZARD_UNINITIALIZED_ACCESS;
    const bool nextWrite = gpuAccessFlagsIsWrite(nextAccess);
    const bool currentWrite = gpuResourceStateIsUav(currentState) ||
        currentState == GPU_RESOURCE_STATE_RENDER_TARGET ||
        currentState == GPU_RESOURCE_STATE_DEPTH_WRITE ||
        currentState == GPU_RESOURCE_STATE_COPY_DEST;
    if (hadWriter && nextWrite) return GPU_HAZARD_WRITE_AFTER_WRITE;
    if (hadWriter && !nextWrite) return GPU_HAZARD_READ_AFTER_WRITE;
    if (!hadWriter && nextWrite && currentWrite) return GPU_HAZARD_WRITE_AFTER_READ;
    return GPU_HAZARD_NONE;
}

const char* gpuHazardKindName(GpuHazardKind kind)
{
    switch (kind) {
    case GPU_HAZARD_READ_AFTER_WRITE: return "read_after_write";
    case GPU_HAZARD_WRITE_AFTER_WRITE: return "write_after_write";
    case GPU_HAZARD_WRITE_AFTER_READ: return "write_after_read";
    case GPU_HAZARD_UNINITIALIZED_ACCESS: return "uninitialized_access";
    default: return "none";
    }
}

static bool accessAllowed(GpuResourceState state, GpuAccessFlags access)
{
    const GpuAccessFlags current = gpuAccessFlagsForResourceState(state);
    if (state == GPU_RESOURCE_STATE_UNDEFINED) return false;
    if (gpuAccessFlagsIsWrite(access)) {
        if (access & GPU_ACCESS_SHADER_WRITE) return true;
        if ((access & GPU_ACCESS_COLOR_ATTACHMENT) && (current & GPU_ACCESS_COLOR_ATTACHMENT)) return true;
        if ((access & GPU_ACCESS_DEPTH_WRITE) && (current & GPU_ACCESS_DEPTH_WRITE)) return true;
        if ((access & GPU_ACCESS_COPY_WRITE) && (current & GPU_ACCESS_COPY_WRITE)) return true;
        return state == GPU_RESOURCE_STATE_COMMON;
    }
    if (access & GPU_ACCESS_SHADER_READ) {
        return (current & GPU_ACCESS_SHADER_READ) != 0 ||
               state == GPU_RESOURCE_STATE_SHADER_RESOURCE ||
               state == GPU_RESOURCE_STATE_DEPTH_READ ||
               state == GPU_RESOURCE_STATE_STORAGE_BUFFER;
    }
    if (access & GPU_ACCESS_COPY_READ) return state == GPU_RESOURCE_STATE_COPY_SOURCE;
    return true;
}

GpuResult gpuCmdValidateBufferAccess(GpuDevice device, GpuCommandEncoder encoder,
                                     GpuBufferHandle buffer, GpuAccessFlags access)
{
    (void)encoder;
    if (!device || buffer.index == 0) return GPU_ERROR_INVALID_ARGS;
    if (device->debugLevel == GPU_DEBUG_LEVEL_NONE) return GPU_SUCCESS;

    GpuResourceState state = gpuGetBufferState(device, buffer);
    if (!accessAllowed(state, access)) {
        GPU_VALIDATE(device, GPU_VALIDATION_SEVERITY_ERROR, "BUFFER_STATE_HAZARD",
                     "Buffer accessed without required state transition", "Buffer");
        return GPU_ERROR_INVALID_ARGS;
    }
    return GPU_SUCCESS;
}

GpuResult gpuCmdValidateTextureAccess(GpuDevice device, GpuCommandEncoder encoder,
                                      GpuTextureHandle texture, GpuAccessFlags access)
{
    (void)encoder;
    if (!device || texture.index == 0) return GPU_ERROR_INVALID_ARGS;
    if (device->debugLevel == GPU_DEBUG_LEVEL_NONE) return GPU_SUCCESS;

    GpuResourceState state = gpuGetTextureState(device, texture);
    if (!accessAllowed(state, access)) {
        GPU_VALIDATE(device, GPU_VALIDATION_SEVERITY_ERROR, "TEXTURE_STATE_HAZARD",
                     "Texture accessed without required state transition", "Texture");
        return GPU_ERROR_INVALID_ARGS;
    }
    return GPU_SUCCESS;
}
