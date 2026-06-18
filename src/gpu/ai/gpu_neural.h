#pragma once

#include "gpu/core/gpu_types.h"
#include "gpu/core/gpu_handle.h"
#include "gpu/core/gpu_device.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/core/gpu_command.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuNeuralNetwork_t* GpuNeuralNetwork;

typedef struct {
    uint32_t inputWidth;
    uint32_t inputHeight;
    uint32_t hiddenDim;
    uint32_t outputChannels;
    GpuTensorFormat weightFormat;
} GpuNeuralNetworkDesc;

GpuResult gpuCreateNeuralNetwork(GpuDevice device, const GpuNeuralNetworkDesc* desc, GpuNeuralNetwork* outNetwork);
GpuResult gpuNeuralNetworkInference(GpuCommandBuffer cmd, GpuNeuralNetwork network, GpuTensorHandle input, GpuTensorHandle output);
GpuResult gpuLoadNeuralNetworkWeights(GpuDevice device, GpuNeuralNetwork network, const void* weightData, size_t weightSize);
void gpuDestroyNeuralNetwork(GpuDevice device, GpuNeuralNetwork network);

#ifdef __cplusplus
}
#endif
