#include "gpu/resource/gpu_resource_manager.h"
#include "gpu/core/gpu_internal.h"
#include <vector>
#include <mutex>

struct PendingRelease {
    GpuHandle handle;
    uint64_t fenceValue;
    uint8_t type;
};

struct GpuResourceManager_t {
    GpuDevice device;
    std::vector<PendingRelease> releaseQueue;
    std::mutex mutex;
};

GpuResult gpuResourceManagerCreate(GpuDevice device, GpuResourceManager* outMgr)
{
    if (!device || !outMgr) return GPU_ERROR_INVALID_ARGS;
    GpuResourceManager mgr = new GpuResourceManager_t();
    mgr->device = device;
    *outMgr = mgr;
    return GPU_SUCCESS;
}

GpuResult gpuResourceManagerFlush(GpuResourceManager mgr, uint64_t completedFenceValue)
{
    if (!mgr) return GPU_ERROR_INVALID_ARGS;
    std::lock_guard<std::mutex> lock(mgr->mutex);

    size_t writeIdx = 0;
    for (size_t i = 0; i < mgr->releaseQueue.size(); i++) {
        auto& pending = mgr->releaseQueue[i];
        if (pending.fenceValue <= completedFenceValue) {
            switch (pending.type) {
            case 0:
                gpuDestroyBuffer(mgr->device, {pending.handle.index, pending.handle.generation});
                break;
            case 1:
                gpuDestroyTexture(mgr->device, {pending.handle.index, pending.handle.generation});
                break;
            }
        } else {
            if (writeIdx != i) {
                mgr->releaseQueue[writeIdx] = pending;
            }
            writeIdx++;
        }
    }
    mgr->releaseQueue.resize(writeIdx);
    return GPU_SUCCESS;
}

void gpuResourceManagerDestroy(GpuResourceManager mgr)
{
    if (!mgr) return;
    for (auto& pending : mgr->releaseQueue) {
        switch (pending.type) {
        case 0:
            gpuDestroyBuffer(mgr->device, {pending.handle.index, pending.handle.generation});
            break;
        case 1:
            gpuDestroyTexture(mgr->device, {pending.handle.index, pending.handle.generation});
            break;
        }
    }
    delete mgr;
}
