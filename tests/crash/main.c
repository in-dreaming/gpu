#include "gpu/gpu.h"
#include <stdio.h>
#include <stdlib.h>
static void flush(void) { fflush(stdout); fflush(stderr); }

int main(void)
{
    GpuResult res;
    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "fence_crash", .enableDebugLayer = false };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("FAIL: %d\n", res); return 1; }
    printf("Device OK\n"); flush();

    GpuCommandQueue queue;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("FAIL q: %d\n", res); return 1; }
    printf("Queue OK\n"); flush();

    GpuFence fence;
    res = gpuCreateFence(device, 0, &fence);
    if (res != GPU_SUCCESS) { printf("FAIL fence: %d\n", res); return 1; }
    printf("Fence OK\n"); flush();

    GpuCommandEncoder enc = gpuBeginCommandEncoder(device, queue);
    if (!enc) { printf("FAIL enc\n"); return 1; }
    GpuCommandBuffer cmd = gpuFinishCommandEncoder(enc);
    if (!cmd) { printf("FAIL cmd\n"); return 1; }
    printf("Cmd OK\n"); flush();

    printf("About to call SubmitWithFence...\n"); flush();
    res = gpuQueueSubmitWithFence(queue, 1, &cmd, fence, 1);
    printf("SubmitWithFence returned: %d\n", res); flush();

    res = gpuFenceWait(device, fence, 1, 5000);
    printf("Wait=%d val=%llu\n", res, (unsigned long long)gpuFenceGetCurrentValue(fence)); flush();

    gpuDestroyFence(device, fence);
    gpuDestroyDevice(device);
    printf("DONE\n"); flush();
    return 0;
}
