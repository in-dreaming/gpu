#include "gpu/ai/gpu_neural.h"
#include "gpu/ai/gpu_tensor.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_internal.h"
#include "gpu/pipeline/gpu_pipeline_state.h"
#include "gpu/shader/gpu_shader_compiler.h"
#include <slang.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

extern rhi::IComputePipeline* gpuResolveComputePipeline(GpuDevice device, GpuPipelineHandle pipeline);

static const char* s_denseLayerShader = R"(
struct PushConstants
{
    uint inputSize;
    uint outputSize;
    uint applyRelu;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pc;

RWStructuredBuffer<float> inputBuffer;
RWStructuredBuffer<float> weightBuffer;
RWStructuredBuffer<float> biasBuffer;
RWStructuredBuffer<float> outputBuffer;

[numthreads(64, 1, 1)]
void main(uint3 threadID : SV_DispatchThreadID)
{
    uint idx = threadID.x;
    if (idx >= pc.outputSize) return;

    float sum = 0.0;
    for (uint i = 0; i < pc.inputSize; i++) {
        sum += inputBuffer[i] * weightBuffer[idx * pc.inputSize + i];
    }
    sum += biasBuffer[idx];

    if (pc.applyRelu != 0 && sum < 0.0) sum = 0.0;

    outputBuffer[idx] = sum;
}
)";

struct NeuralLayer {
    GpuTensorHandle weights;
    GpuTensorHandle biases;
    uint32_t inputSize;
    uint32_t outputSize;
};

struct GpuNeuralNetwork_t {
    uint32_t inputWidth;
    uint32_t inputHeight;
    uint32_t hiddenDim;
    uint32_t outputChannels;
    GpuTensorFormat weightFormat;

    NeuralLayer* layers;
    uint32_t layerCount;

    GpuTensorHandle hiddenActivations;

    GpuPipelineHandle densePipeline;
    GpuShaderProgram denseProgram;
    GpuShaderCompiler denseCompiler;
    bool pipelineReady;
};

static bool writeNeuralShaderToTempFile(std::string& outPath) {
    return gpuWriteTextTempFile("gpu_neural_dense.slang", s_denseLayerShader, outPath);
}

static GpuResult ensureDensePipeline(GpuDevice device, GpuNeuralNetwork_t* net) {
    if (net->pipelineReady) return GPU_SUCCESS;

    GpuResult res = gpuCreateShaderCompiler(device, &net->denseCompiler);
    if (res != GPU_SUCCESS) return res;

    std::string shaderPath;
    if (!writeNeuralShaderToTempFile(shaderPath)) {
        gpuDestroyShaderCompiler(net->denseCompiler);
        net->denseCompiler = NULL;
        return GPU_ERROR_INTERNAL;
    }

    GpuShaderCompileDesc compileDesc = {};
    compileDesc.sourcePath = shaderPath.c_str();
    compileDesc.entryPoint = "main";
    compileDesc.target = GPU_SHADER_TARGET_SPIRV;

    res = gpuCompileShader(net->denseCompiler, &compileDesc, &net->denseProgram);
    if (res != GPU_SUCCESS) {
        gpuDestroyShaderCompiler(net->denseCompiler);
        net->denseCompiler = NULL;
        return res;
    }

    res = gpuCreateComputePipelineFromProgram(device, net->denseProgram, "DenseLayer", &net->densePipeline);
    if (res != GPU_SUCCESS) {
        gpuDestroyShaderProgram(net->denseProgram);
        net->denseProgram = NULL;
        gpuDestroyShaderCompiler(net->denseCompiler);
        net->denseCompiler = NULL;
        return res;
    }

    net->pipelineReady = true;
    return GPU_SUCCESS;
}

GpuResult gpuCreateNeuralNetwork(GpuDevice device, const GpuNeuralNetworkDesc* desc, GpuNeuralNetwork* outNetwork)
{
    if (!device || !desc || !outNetwork) return GPU_ERROR_INVALID_ARGS;
    if (desc->inputWidth == 0 || desc->inputHeight == 0 || desc->hiddenDim == 0 || desc->outputChannels == 0) {
        return GPU_ERROR_INVALID_ARGS;
    }

    GpuNeuralNetwork net = (GpuNeuralNetwork)calloc(1, sizeof(GpuNeuralNetwork_t));
    if (!net) return GPU_ERROR_OUT_OF_MEMORY;

    net->inputWidth = desc->inputWidth;
    net->inputHeight = desc->inputHeight;
    net->hiddenDim = desc->hiddenDim;
    net->outputChannels = desc->outputChannels;
    net->weightFormat = desc->weightFormat;
    net->pipelineReady = false;

    uint32_t inputSize = desc->inputWidth * desc->inputHeight * 3;
    uint32_t outputSize = desc->inputWidth * desc->inputHeight * desc->outputChannels;

    net->layerCount = 2;
    net->layers = (NeuralLayer*)malloc(sizeof(NeuralLayer) * net->layerCount);
    if (!net->layers) {
        free(net);
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    GpuResult res;

    {
        NeuralLayer& layer = net->layers[0];
        layer.inputSize = inputSize;
        layer.outputSize = desc->hiddenDim;

        GpuTensorDesc weightDesc = {};
        weightDesc.format = desc->weightFormat;
        weightDesc.dimCount = 2;
        weightDesc.dims[0] = desc->hiddenDim;
        weightDesc.dims[1] = inputSize;
        weightDesc.strides[0] = inputSize;
        weightDesc.strides[1] = 1;

        res = gpuCreateTensor(device, &weightDesc, &layer.weights);
        if (res != GPU_SUCCESS) {
            free(net->layers);
            free(net);
            return res;
        }

        GpuTensorDesc biasDesc = {};
        biasDesc.format = desc->weightFormat;
        biasDesc.dimCount = 1;
        biasDesc.dims[0] = desc->hiddenDim;
        biasDesc.strides[0] = 1;

        res = gpuCreateTensor(device, &biasDesc, &layer.biases);
        if (res != GPU_SUCCESS) {
            gpuDestroyTensor(device, layer.weights);
            free(net->layers);
            free(net);
            return res;
        }
    }

    {
        NeuralLayer& layer = net->layers[1];
        layer.inputSize = desc->hiddenDim;
        layer.outputSize = outputSize;

        GpuTensorDesc weightDesc = {};
        weightDesc.format = desc->weightFormat;
        weightDesc.dimCount = 2;
        weightDesc.dims[0] = outputSize;
        weightDesc.dims[1] = desc->hiddenDim;
        weightDesc.strides[0] = desc->hiddenDim;
        weightDesc.strides[1] = 1;

        res = gpuCreateTensor(device, &weightDesc, &layer.weights);
        if (res != GPU_SUCCESS) {
            gpuDestroyTensor(device, net->layers[0].weights);
            gpuDestroyTensor(device, net->layers[0].biases);
            free(net->layers);
            free(net);
            return res;
        }

        GpuTensorDesc biasDesc = {};
        biasDesc.format = desc->weightFormat;
        biasDesc.dimCount = 1;
        biasDesc.dims[0] = outputSize;
        biasDesc.strides[0] = 1;

        res = gpuCreateTensor(device, &biasDesc, &layer.biases);
        if (res != GPU_SUCCESS) {
            gpuDestroyTensor(device, layer.weights);
            gpuDestroyTensor(device, net->layers[0].weights);
            gpuDestroyTensor(device, net->layers[0].biases);
            free(net->layers);
            free(net);
            return res;
        }
    }

    {
        GpuTensorDesc hiddenDesc = {};
        hiddenDesc.format = GPU_TENSOR_FORMAT_F32;
        hiddenDesc.dimCount = 1;
        hiddenDesc.dims[0] = desc->hiddenDim;
        hiddenDesc.strides[0] = 1;

        res = gpuCreateTensor(device, &hiddenDesc, &net->hiddenActivations);
        if (res != GPU_SUCCESS) {
            gpuDestroyTensor(device, net->layers[1].weights);
            gpuDestroyTensor(device, net->layers[1].biases);
            gpuDestroyTensor(device, net->layers[0].weights);
            gpuDestroyTensor(device, net->layers[0].biases);
            free(net->layers);
            free(net);
            return res;
        }
    }

    *outNetwork = net;
    return GPU_SUCCESS;
}

GpuResult gpuNeuralNetworkInference(GpuCommandBuffer cmd, GpuNeuralNetwork network, GpuTensorHandle input, GpuTensorHandle output)
{
    if (!cmd || !cmd->device || !network) return GPU_ERROR_INVALID_ARGS;

    GpuNeuralNetwork net = (GpuNeuralNetwork)network;

    GpuBufferHandle inputBuf = gpuGetTensorBuffer(cmd->device, input);
    GpuBufferHandle outputBuf = gpuGetTensorBuffer(cmd->device, output);
    if (!inputBuf.index || !outputBuf.index) return GPU_ERROR_INVALID_ARGS;

    GpuResult res = ensureDensePipeline(cmd->device, net);
    if (res != GPU_SUCCESS) {
        fprintf(stderr, "gpuNeuralNetworkInference: pipeline not ready (%d)\n", res);
        return res;
    }

    if (!cmd->rhiEncoder) return GPU_ERROR_INTERNAL;

    if (!cmd->inComputePass) {
        cmd->computePassEncoder = cmd->rhiEncoder->beginComputePass();
        if (!cmd->computePassEncoder) return GPU_ERROR_INTERNAL;
        cmd->inComputePass = true;
    }

    rhi::IComputePipeline* pipe = gpuResolveComputePipeline(cmd->device, net->densePipeline);
    if (!pipe) return GPU_ERROR_INTERNAL;

    cmd->computePassEncoder->bindPipeline(pipe);

    for (uint32_t i = 0; i < net->layerCount; i++) {
        NeuralLayer& layer = net->layers[i];
        uint32_t dispatchX = (layer.outputSize + 63) / 64;
        cmd->computePassEncoder->dispatchCompute(dispatchX, 1, 1);
    }

    return GPU_SUCCESS;
}

GpuResult gpuLoadNeuralNetworkWeights(GpuDevice device, GpuNeuralNetwork network, const void* weightData, size_t weightSize)
{
    if (!device || !network || !weightData || weightSize == 0) return GPU_ERROR_INVALID_ARGS;

    GpuNeuralNetwork net = (GpuNeuralNetwork)network;

    uint32_t inputSize = net->inputWidth * net->inputHeight * 3;
    uint32_t outputSize = net->inputWidth * net->inputHeight * net->outputChannels;

    size_t elementSize = (net->weightFormat == GPU_TENSOR_FORMAT_F32) ? 4 : 2;
    size_t layer1Weights = net->hiddenDim * inputSize * elementSize;
    size_t layer1Biases = net->hiddenDim * elementSize;
    size_t layer2Weights = outputSize * net->hiddenDim * elementSize;
    size_t layer2Biases = outputSize * elementSize;
    size_t totalSize = layer1Weights + layer1Biases + layer2Weights + layer2Biases;

    if (weightSize < totalSize) {
        fprintf(stderr, "gpuLoadNeuralNetworkWeights: Weight data too small (%zu < %zu)\n",
                weightSize, totalSize);
        return GPU_ERROR_INVALID_ARGS;
    }

    GpuBufferHandle w1Buffer = gpuGetTensorBuffer(device, net->layers[0].weights);
    GpuBufferHandle b1Buffer = gpuGetTensorBuffer(device, net->layers[0].biases);
    GpuBufferHandle w2Buffer = gpuGetTensorBuffer(device, net->layers[1].weights);
    GpuBufferHandle b2Buffer = gpuGetTensorBuffer(device, net->layers[1].biases);

    if (!w1Buffer.index || !b1Buffer.index || !w2Buffer.index || !b2Buffer.index) {
        return GPU_ERROR_INVALID_ARGS;
    }

    size_t offset = 0;
    const uint8_t* data = (const uint8_t*)weightData;

    GpuResult res = gpuUploadToBuffer(device, w1Buffer, data + offset, layer1Weights, 0);
    if (res != GPU_SUCCESS) return res;
    offset += layer1Weights;

    res = gpuUploadToBuffer(device, b1Buffer, data + offset, layer1Biases, 0);
    if (res != GPU_SUCCESS) return res;
    offset += layer1Biases;

    res = gpuUploadToBuffer(device, w2Buffer, data + offset, layer2Weights, 0);
    if (res != GPU_SUCCESS) return res;
    offset += layer2Weights;

    res = gpuUploadToBuffer(device, b2Buffer, data + offset, layer2Biases, 0);
    return res;
}

void gpuDestroyNeuralNetwork(GpuDevice device, GpuNeuralNetwork network)
{
    if (!device || !network) return;

    GpuNeuralNetwork net = (GpuNeuralNetwork)network;

    for (uint32_t i = 0; i < net->layerCount; i++) {
        gpuDestroyTensor(device, net->layers[i].weights);
        gpuDestroyTensor(device, net->layers[i].biases);
    }

    gpuDestroyTensor(device, net->hiddenActivations);

    if (net->pipelineReady) {
        gpuDestroyPipeline(device, net->densePipeline);
        if (net->denseProgram) gpuDestroyShaderProgram(net->denseProgram);
        if (net->denseCompiler) gpuDestroyShaderCompiler(net->denseCompiler);
    }

    free(net->layers);
    free(net);
}
