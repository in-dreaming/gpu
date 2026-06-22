#include "gpu/core/gpu_sampler.h"
#include "gpu/core/gpu_internal.h"
#include <string.h>

static rhi::TextureFilteringMode gpuFilterToRhi(GpuFilter f)
{
    return (f == GPU_FILTER_LINEAR) ? rhi::TextureFilteringMode::Linear : rhi::TextureFilteringMode::Point;
}

static rhi::TextureAddressingMode gpuAddressModeToRhi(GpuSamplerAddressMode m)
{
    switch (m) {
    case GPU_SAMPLER_ADDRESS_MODE_REPEAT:          return rhi::TextureAddressingMode::Wrap;
    case GPU_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT: return rhi::TextureAddressingMode::MirrorRepeat;
    case GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:   return rhi::TextureAddressingMode::ClampToEdge;
    case GPU_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER: return rhi::TextureAddressingMode::ClampToBorder;
    default:                                        return rhi::TextureAddressingMode::Wrap;
    }
}

static rhi::TextureReductionOp gpuReductionModeToRhi(GpuSamplerReductionMode m, bool compareEnable)
{
    if (compareEnable) return rhi::TextureReductionOp::Comparison;
    switch (m) {
    case GPU_SAMPLER_REDUCTION_MODE_MIN:     return rhi::TextureReductionOp::Minimum;
    case GPU_SAMPLER_REDUCTION_MODE_MAX:     return rhi::TextureReductionOp::Maximum;
    default:                                  return rhi::TextureReductionOp::Average;
    }
}

static rhi::ComparisonFunc gpuCompareOpToRhiSampler(uint32_t op)
{
    switch (op) {
    case 0:  return rhi::ComparisonFunc::Never;
    case 1:  return rhi::ComparisonFunc::Less;
    case 2:  return rhi::ComparisonFunc::Equal;
    case 3:  return rhi::ComparisonFunc::LessEqual;
    case 4:  return rhi::ComparisonFunc::Greater;
    case 5:  return rhi::ComparisonFunc::NotEqual;
    case 6:  return rhi::ComparisonFunc::GreaterEqual;
    case 7:  return rhi::ComparisonFunc::Always;
    default: return rhi::ComparisonFunc::Never;
    }
}

GpuResult gpuCreateSampler(GpuDevice device, const GpuSamplerDesc* desc, GpuSamplerHandle* outSampler)
{
    if (!device || !desc || !outSampler) return GPU_ERROR_INVALID_ARGS;

    rhi::SamplerDesc rhiDesc = {};
    rhiDesc.minFilter = gpuFilterToRhi(desc->minFilter);
    rhiDesc.magFilter = gpuFilterToRhi(desc->magFilter);
    rhiDesc.mipFilter = gpuFilterToRhi(desc->mipFilter);
    rhiDesc.reductionOp = gpuReductionModeToRhi(desc->reductionMode, desc->compareEnable);
    rhiDesc.addressU = gpuAddressModeToRhi(desc->addressModeU);
    rhiDesc.addressV = gpuAddressModeToRhi(desc->addressModeV);
    rhiDesc.addressW = gpuAddressModeToRhi(desc->addressModeW);
    rhiDesc.mipLODBias = desc->lodBias;
    rhiDesc.maxAnisotropy = desc->maxAnisotropy;
    rhiDesc.comparisonFunc = desc->compareEnable ? gpuCompareOpToRhiSampler(desc->compareOp) : rhi::ComparisonFunc::Never;
    if (desc->borderColor[0] != 0 || desc->borderColor[1] != 0 ||
        desc->borderColor[2] != 0 || desc->borderColor[3] != 0) {
        rhiDesc.borderColor[0] = desc->borderColor[0];
        rhiDesc.borderColor[1] = desc->borderColor[1];
        rhiDesc.borderColor[2] = desc->borderColor[2];
        rhiDesc.borderColor[3] = desc->borderColor[3];
    }
    rhiDesc.minLOD = desc->minLod;
    rhiDesc.maxLOD = desc->maxLod;
    rhiDesc.label = desc->label;

    rhi::ComPtr<rhi::ISampler> rhiSampler;
    if (SLANG_FAILED(device->rhiDevice->createSampler(rhiDesc, rhiSampler.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    uint32_t idx = device->samplerPool.allocate(rhiSampler.detach());
    if (idx == 0) return GPU_ERROR_OUT_OF_MEMORY;

    *outSampler = {idx, device->samplerPool.slots[idx].generation};
    return GPU_SUCCESS;
}

void gpuDestroySampler(GpuDevice device, GpuSamplerHandle sampler)
{
    if (!device || sampler.index == 0) return;
    rhi::ISampler* rhiSampler = device->samplerPool.resolve(sampler.index, sampler.generation);
    if (rhiSampler) {
        rhiSampler->release();
    }
    device->samplerPool.release(sampler.index, sampler.generation);
}
