#include "gpu/debug/gpu_debug.h"
#include "gpu/core/gpu_internal.h"
#include <string>

static thread_local std::string s_lastError;
static GpuDebugCallback s_debugCallback = nullptr;
static void* s_debugUserData = nullptr;
static GpuDebugLevel s_debugLevel = GPU_DEBUG_LEVEL_NONE;

void gpuSetDebugCallback(GpuDevice device, GpuDebugCallback callback, void* userData)
{
    s_debugCallback = callback;
    s_debugUserData = userData;
    (void)device;
}

const char* gpuGetLastError(void)
{
    return s_lastError.empty() ? nullptr : s_lastError.c_str();
}

GpuDebugLevel gpuGetDebugLevel(GpuDevice device)
{
    (void)device;
    return s_debugLevel;
}

void gpuSetDebugLevel(GpuDevice device, GpuDebugLevel level)
{
    (void)device;
    s_debugLevel = level;
}
