#include "gpu/gpu.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/ai/gpu_neural.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    GpuResult res = gpuPlatformInit();
    if (res != GPU_SUCCESS) { printf("Platform init failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuDevice device = NULL;
    GpuDeviceDesc devDesc = { .appName = "16_neural_render", .enableDebugLayer = true };
    res = gpuCreateDevice(&devDesc, &device);
    if (res != GPU_SUCCESS) { printf("Device failed: %d\n", res); gpuPlatformShutdown(); return 1; }

    GpuCommandQueue queue = NULL;
    res = gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    if (res != GPU_SUCCESS) { printf("Queue failed: %d\n", res); gpuDestroyDevice(device); gpuPlatformShutdown(); return 1; }

    printf("Creating neural network...\n");

    GpuNeuralNetworkDesc nnDesc = {
        .inputWidth = 256,
        .inputHeight = 256,
        .hiddenDim = 64,
        .outputChannels = 3,
        .weightFormat = GPU_TENSOR_FORMAT_F16,
    };

    GpuNeuralNetwork neuralNet;
    res = gpuCreateNeuralNetwork(device, &nnDesc, &neuralNet);
    if (res != GPU_SUCCESS) {
        printf("Neural network creation unavailable on this backend/configuration: %d\n", res);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }

    GpuTensorDesc inputDesc = {
        .format = GPU_TENSOR_FORMAT_F16,
        .dimCount = 4,
        .dims = {1, 256, 256, 3},
        .strides = {256*256*3, 256*3, 3, 1}
    };
    GpuTensorDesc outputDesc = {
        .format = GPU_TENSOR_FORMAT_F16,
        .dimCount = 4,
        .dims = {1, 256, 256, 3},
        .strides = {256*256*3, 256*3, 3, 1}
    };

    GpuTensorHandle inputTensor = {0, 0};
    GpuTensorHandle outputTensor = {0, 0};
    res = gpuCreateTensor(device, &inputDesc, &inputTensor);
    if (res != GPU_SUCCESS) {
        printf("Input tensor creation failed: %d\n", res);
        gpuDestroyNeuralNetwork(device, neuralNet);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }
    res = gpuCreateTensor(device, &outputDesc, &outputTensor);
    if (res != GPU_SUCCESS) {
        printf("Output tensor creation failed: %d\n", res);
        gpuDestroyTensor(device, inputTensor);
        gpuDestroyNeuralNetwork(device, neuralNet);
        gpuDestroyDevice(device);
        gpuPlatformShutdown();
        return 1;
    }

    printf("Network config: input=%dx%dx%d, hidden=%d, output=%d channels\n",
           nnDesc.inputWidth, nnDesc.inputHeight, nnDesc.inputWidth,
           nnDesc.hiddenDim, nnDesc.outputChannels);

    printf("\nNeural rendering test completed.\n");

    gpuDestroyNeuralNetwork(device, neuralNet);
    gpuDestroyTensor(device, inputTensor);
    gpuDestroyTensor(device, outputTensor);
    gpuDestroyDevice(device);
    gpuPlatformShutdown();

    return 0;
}
