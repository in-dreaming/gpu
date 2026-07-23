#ifdef _MSC_VER
#pragma warning(disable : 4819)
#endif
#include "gpu/debug/gpu_debug.h"
#include "gpu/debug/gpu_validation.h"
#include "gpu/core/gpu_internal.h"
#include <string>
#include <cstdio>

// ============================================================================
// RHI Debug Callback bridge — forwards driver/layer messages to user callback
// ============================================================================

GpuRhiDebugCallback::GpuRhiDebugCallback(GpuDevice device)
    : m_device(device)
{}

void SLANG_MCALL GpuRhiDebugCallback::handleMessage(
    rhi::DebugMessageType type,
    rhi::DebugMessageSource source,
    const char* message)
{
    if (!m_device || !message) return;

    GpuDebugLevel level = GPU_DEBUG_LEVEL_INFO;
    if (type == rhi::DebugMessageType::Error) level = GPU_DEBUG_LEVEL_ERROR;
    else if (type == rhi::DebugMessageType::Warning) level = GPU_DEBUG_LEVEL_WARNING;

    if (type == rhi::DebugMessageType::Error || type == rhi::DebugMessageType::Warning) {
        GpuValidationSeverity severity = (type == rhi::DebugMessageType::Error)
            ? GPU_VALIDATION_SEVERITY_ERROR
            : GPU_VALIDATION_SEVERITY_WARNING;
        gpuEmitValidation(m_device, severity, "RHI_DEBUG", message, nullptr, "RHI", nullptr, 0);
    }

    void (*callback)(GpuDebugLevel, const char*, void*) = nullptr;
    void* userData = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_device->debugMutex);
        callback = m_device->debugCallback;
        userData = m_device->debugUserData;
    }
    if (callback) {
        char sourceMessage[1024];
        const char* sourceName = (source == rhi::DebugMessageSource::Layer) ? "Layer"
                               : (source == rhi::DebugMessageSource::Driver) ? "Driver"
                               : "Slang";
        snprintf(sourceMessage, sizeof(sourceMessage), "[%s] %s", sourceName, message);
        callback(level, sourceMessage, userData);
    }
}

// ============================================================================
// Public API
// ============================================================================

void gpuSetValidationCallback(GpuDevice device, GpuValidationCallback callback, void* userData)
{
    if (!device) return;
    std::lock_guard<std::mutex> lock(device->debugMutex);
    device->validationCallback = callback;
    device->validationUserData = userData;
}

void gpuSetDebugCallback(GpuDevice device, void (*callback)(GpuDebugLevel, const char*, void*), void* userData)
{
    if (!device) return;

    {
        std::lock_guard<std::mutex> lock(device->debugMutex);
        device->debugCallback = callback;
        device->debugUserData = userData;
    }

}

const char* gpuGetLastError(void)
{
    // This is a global query — search all devices for the last error
    // For thread safety, we use a static buffer
    static thread_local std::string s_error;
    // Try the most recently created device's error
    // Since we don't track "most recent", we just return a generic message
    s_error = "use gpuGetLastErrorForDevice for per-device errors";
    return s_error.c_str();
}

GpuDebugLevel gpuGetDebugLevel(GpuDevice device)
{
    if (!device) return GPU_DEBUG_LEVEL_NONE;
    return device->debugLevel;
}

void gpuSetDebugLevel(GpuDevice device, GpuDebugLevel level)
{
    if (!device) return;
    std::lock_guard<std::mutex> lock(device->debugMutex);
    device->debugLevel = level;
}

bool gpuIsValidationEnabled(GpuDevice device)
{
    if (!device) return false;
    std::lock_guard<std::mutex> lock(device->debugMutex);
    return device->validationCallback != nullptr || device->debugLevel >= GPU_DEBUG_LEVEL_ERROR;
}

void gpuEmitValidation(GpuDevice device, GpuValidationSeverity severity, const char* messageId,
                        const char* message, const char* objectLabel,
                        const char* function, const char* file, int line)
{
    if (!device || !message) return;

    GpuValidationCallback validationCallback = nullptr;
    void* validationUserData = nullptr;
    GpuDebugLevel debugLevel = GPU_DEBUG_LEVEL_NONE;
    {
        std::lock_guard<std::mutex> lock(device->debugMutex);
        device->lastError = message;
        validationCallback = device->validationCallback;
        validationUserData = device->validationUserData;
        debugLevel = device->debugLevel;
    }

    if (validationCallback) {
        GpuValidationMessage msg = {};
        msg.severity = severity;
        msg.messageId = messageId;
        msg.message = message;
        msg.objectLabel = objectLabel;
        msg.function = function;
        msg.file = file;
        msg.line = line;
        validationCallback(&msg, validationUserData);
    }

    if (debugLevel >= GPU_DEBUG_LEVEL_ERROR) {
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
