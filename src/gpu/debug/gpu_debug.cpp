#include "gpu/debug/gpu_debug.h"
#include "gpu/debug/gpu_validation.h"
#include "gpu/core/gpu_internal.h"
#include <string>
#include <cstdio>
#include <cstdarg>

static thread_local std::string s_lastError;
static GpuValidationCallback s_validationCallback = nullptr;
static void* s_validationUserData = nullptr;
static GpuDebugLevel s_debugLevel = GPU_DEBUG_LEVEL_NONE;

void gpuSetValidationCallback(GpuDevice device, GpuValidationCallback callback, void* userData)
{
    (void)device;
    s_validationCallback = callback;
    s_validationUserData = userData;
}

void gpuSetDebugCallback(GpuDevice device, void (*callback)(GpuDebugLevel, const char*, void*), void* userData)
{
    (void)device;
    (void)callback;
    (void)userData;
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

bool gpuIsValidationEnabled(GpuDevice device)
{
    (void)device;
    return s_validationCallback != nullptr || s_debugLevel >= GPU_DEBUG_LEVEL_ERROR;
}

void gpuEmitValidation(GpuValidationSeverity severity, const char* messageId,
                        const char* message, const char* objectLabel,
                        const char* function, const char* file, int line)
{
    s_lastError = message;

    if (s_validationCallback) {
        GpuValidationMessage msg = {};
        msg.severity = severity;
        msg.messageId = messageId;
        msg.message = message;
        msg.objectLabel = objectLabel;
        msg.function = function;
        msg.file = file;
        msg.line = line;
        s_validationCallback(&msg, s_validationUserData);
    }

    if (s_debugLevel >= GPU_DEBUG_LEVEL_ERROR) {
        const char* sevStr = (severity == GPU_VALIDATION_SEVERITY_ERROR) ? "ERROR"
                           : (severity == GPU_VALIDATION_SEVERITY_WARNING) ? "WARNING"
                           : "INFO";
        if (objectLabel && objectLabel[0]) {
            fprintf(stderr, "[GPU %s] %s: %s (object: %s)\n", sevStr, messageId, message, objectLabel);
        } else {
            fprintf(stderr, "[GPU %s] %s: %s\n", sevStr, messageId, message);
        }
    }
}
