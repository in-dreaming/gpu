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

class GpuRhiDebugCallback : public rhi::IDebugCallback
{
public:
    GpuDevice m_device = nullptr;

    GpuRhiDebugCallback(GpuDevice device) : m_device(device) {}

    virtual void SLANG_MCALL handleMessage(
        rhi::DebugMessageType type,
        rhi::DebugMessageSource source,
        const char* message) override
    {
        if (!m_device || !message) return;

        GpuDebugLevel level = GPU_DEBUG_LEVEL_INFO;
        if (type == rhi::DebugMessageType::Error) level = GPU_DEBUG_LEVEL_ERROR;
        else if (type == rhi::DebugMessageType::Warning) level = GPU_DEBUG_LEVEL_WARNING;

        // Forward to user debug callback
        std::lock_guard<std::mutex> lock(m_device->debugMutex);
        if (m_device->debugCallback) {
            char sourceMsg[256];
            const char* srcStr = (source == rhi::DebugMessageSource::Layer) ? "Layer"
                               : (source == rhi::DebugMessageSource::Driver) ? "Driver"
                               : "Slang";
            snprintf(sourceMsg, sizeof(sourceMsg), "[%s] %s", srcStr, message);
            m_device->debugCallback(level, sourceMsg, m_device->debugUserData);
        }

        // Also emit as validation if severity is error/warning
        if (type == rhi::DebugMessageType::Error || type == rhi::DebugMessageType::Warning) {
            GpuValidationSeverity sev = (type == rhi::DebugMessageType::Error)
                ? GPU_VALIDATION_SEVERITY_ERROR : GPU_VALIDATION_SEVERITY_WARNING;
            gpuEmitValidation(m_device, sev, "RHI_DEBUG", message, nullptr, "RHI", nullptr, 0);
        }
    }
};

// One callback per device (created lazily)
static std::map<GpuDevice, rhi::ComPtr<GpuRhiDebugCallback>> s_rhiCallbacks;
static std::mutex s_rhiCallbackMutex;

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

    // Wire the RHI debug callback to receive driver/layer messages
    if (callback) {
        std::lock_guard<std::mutex> cbLock(s_rhiCallbackMutex);
        auto it = s_rhiCallbacks.find(device);
        if (it == s_rhiCallbacks.end()) {
            auto* rhiCb = new GpuRhiDebugCallback(device);
            s_rhiCallbacks[device] = rhiCb;
            // Note: slang-rhi DeviceDesc.debugCallback is set at device creation time.
            // For existing devices, we can't retroactively add it. But the callback
            // object is stored and can be used if the device was created with it.
        }
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

    std::lock_guard<std::mutex> lock(device->debugMutex);
    device->lastError = message;

    if (device->validationCallback) {
        GpuValidationMessage msg = {};
        msg.severity = severity;
        msg.messageId = messageId;
        msg.message = message;
        msg.objectLabel = objectLabel;
        msg.function = function;
        msg.file = file;
        msg.line = line;
        device->validationCallback(&msg, device->validationUserData);
    }

    if (device->debugLevel >= GPU_DEBUG_LEVEL_ERROR) {
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
