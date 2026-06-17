#pragma once

#include <slang-rhi.h>
#include "gpu/core/gpu_format.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_texture.h"
#include "gpu/core/gpu_handle_pool.h"

struct GpuDevice_t {
    rhi::ComPtr<rhi::IDevice> rhiDevice;
    rhi::ComPtr<rhi::ICommandQueue> graphicsQueue;
    GpuHandlePool<rhi::IBuffer> bufferPool;
    GpuHandlePool<rhi::ITexture> texturePool;
};

struct GpuCommandEncoder_t {
    rhi::ComPtr<rhi::ICommandEncoder> rhiEncoder;
    rhi::ICommandQueue* queue;
};

struct GpuCommandBuffer_t {
    rhi::ComPtr<rhi::ICommandBuffer> rhiCmdBuffer;
};

struct GpuSurface_t {
    rhi::ComPtr<rhi::ISurface> rhiSurface;
    uint32_t width;
    uint32_t height;
    GpuFormat format;
    bool configured;
};

struct GpuSurfaceTexture_t {
    rhi::ComPtr<rhi::ITexture> rhiTexture;
};

static inline rhi::Format gpuFormatToRhi(GpuFormat fmt)
{
    switch (fmt) {
    case GPU_FORMAT_R8_UNORM:         return rhi::Format::R8Unorm;
    case GPU_FORMAT_R8_SNORM:         return rhi::Format::R8Snorm;
    case GPU_FORMAT_R8_UINT:          return rhi::Format::R8Uint;
    case GPU_FORMAT_R8_SINT:          return rhi::Format::R8Sint;
    case GPU_FORMAT_RG8_UNORM:        return rhi::Format::RG8Unorm;
    case GPU_FORMAT_RG8_SNORM:        return rhi::Format::RG8Snorm;
    case GPU_FORMAT_RG8_UINT:         return rhi::Format::RG8Uint;
    case GPU_FORMAT_RG8_SINT:         return rhi::Format::RG8Sint;
    case GPU_FORMAT_RGBA8_UNORM:      return rhi::Format::RGBA8Unorm;
    case GPU_FORMAT_RGBA8_UNORM_SRGB: return rhi::Format::RGBA8UnormSrgb;
    case GPU_FORMAT_RGBA8_SNORM:      return rhi::Format::RGBA8Snorm;
    case GPU_FORMAT_RGBA8_UINT:       return rhi::Format::RGBA8Uint;
    case GPU_FORMAT_RGBA8_SINT:       return rhi::Format::RGBA8Sint;
    case GPU_FORMAT_BGRA8_UNORM:      return rhi::Format::BGRA8Unorm;
    case GPU_FORMAT_BGRA8_UNORM_SRGB: return rhi::Format::BGRA8UnormSrgb;
    case GPU_FORMAT_R16_UNORM:        return rhi::Format::R16Unorm;
    case GPU_FORMAT_R16_SNORM:        return rhi::Format::R16Snorm;
    case GPU_FORMAT_R16_UINT:         return rhi::Format::R16Uint;
    case GPU_FORMAT_R16_SINT:         return rhi::Format::R16Sint;
    case GPU_FORMAT_R16_FLOAT:        return rhi::Format::R16Float;
    case GPU_FORMAT_RG16_UNORM:       return rhi::Format::RG16Unorm;
    case GPU_FORMAT_RG16_SNORM:       return rhi::Format::RG16Snorm;
    case GPU_FORMAT_RG16_UINT:        return rhi::Format::RG16Uint;
    case GPU_FORMAT_RG16_SINT:        return rhi::Format::RG16Sint;
    case GPU_FORMAT_RG16_FLOAT:       return rhi::Format::RG16Float;
    case GPU_FORMAT_RGBA16_UNORM:     return rhi::Format::RGBA16Unorm;
    case GPU_FORMAT_RGBA16_SNORM:     return rhi::Format::RGBA16Snorm;
    case GPU_FORMAT_RGBA16_UINT:      return rhi::Format::RGBA16Uint;
    case GPU_FORMAT_RGBA16_SINT:      return rhi::Format::RGBA16Sint;
    case GPU_FORMAT_RGBA16_FLOAT:     return rhi::Format::RGBA16Float;
    case GPU_FORMAT_R32_UINT:         return rhi::Format::R32Uint;
    case GPU_FORMAT_R32_SINT:         return rhi::Format::R32Sint;
    case GPU_FORMAT_R32_FLOAT:        return rhi::Format::R32Float;
    case GPU_FORMAT_RG32_UINT:        return rhi::Format::RG32Uint;
    case GPU_FORMAT_RG32_SINT:        return rhi::Format::RG32Sint;
    case GPU_FORMAT_RG32_FLOAT:       return rhi::Format::RG32Float;
    case GPU_FORMAT_RGB32_UINT:       return rhi::Format::RGB32Uint;
    case GPU_FORMAT_RGB32_SINT:       return rhi::Format::RGB32Sint;
    case GPU_FORMAT_RGB32_FLOAT:      return rhi::Format::RGB32Float;
    case GPU_FORMAT_RGBA32_UINT:      return rhi::Format::RGBA32Uint;
    case GPU_FORMAT_RGBA32_SINT:      return rhi::Format::RGBA32Sint;
    case GPU_FORMAT_RGBA32_FLOAT:     return rhi::Format::RGBA32Float;
    case GPU_FORMAT_D16_UNORM:        return rhi::Format::D16Unorm;
    case GPU_FORMAT_D32_FLOAT:        return rhi::Format::D32Float;
    case GPU_FORMAT_D32_FLOAT_S8_UINT: return rhi::Format::D32FloatS8Uint;
    default:                          return rhi::Format::Undefined;
    }
}

static inline uint32_t gpuBufferUsageToRhi(GpuBufferUsage usage)
{
    uint32_t rhiUsage = 0;
    if (usage & GPU_BUFFER_USAGE_VERTEX_BUFFER)          rhiUsage |= (uint32_t)rhi::BufferUsage::VertexBuffer;
    if (usage & GPU_BUFFER_USAGE_INDEX_BUFFER)           rhiUsage |= (uint32_t)rhi::BufferUsage::IndexBuffer;
    if (usage & GPU_BUFFER_USAGE_CONSTANT_BUFFER)        rhiUsage |= (uint32_t)rhi::BufferUsage::ConstantBuffer;
    if (usage & GPU_BUFFER_USAGE_SHADER_RESOURCE)        rhiUsage |= (uint32_t)rhi::BufferUsage::ShaderResource;
    if (usage & GPU_BUFFER_USAGE_UNORDERED_ACCESS)       rhiUsage |= (uint32_t)rhi::BufferUsage::UnorderedAccess;
    if (usage & GPU_BUFFER_USAGE_INDIRECT_ARGUMENT)      rhiUsage |= (uint32_t)rhi::BufferUsage::IndirectArgument;
    if (usage & GPU_BUFFER_USAGE_COPY_SOURCE)            rhiUsage |= (uint32_t)rhi::BufferUsage::CopySource;
    if (usage & GPU_BUFFER_USAGE_COPY_DEST)              rhiUsage |= (uint32_t)rhi::BufferUsage::CopyDestination;
    if (usage & GPU_BUFFER_USAGE_ACCELERATION_STRUCTURE) rhiUsage |= (uint32_t)rhi::BufferUsage::AccelerationStructure;
    return rhiUsage;
}

static inline uint32_t gpuTextureUsageToRhi(GpuTextureUsage usage)
{
    uint32_t rhiUsage = 0;
    if (usage & GPU_TEXTURE_USAGE_SHADER_RESOURCE)  rhiUsage |= (uint32_t)rhi::TextureUsage::ShaderResource;
    if (usage & GPU_TEXTURE_USAGE_UNORDERED_ACCESS) rhiUsage |= (uint32_t)rhi::TextureUsage::UnorderedAccess;
    if (usage & GPU_TEXTURE_USAGE_RENDER_TARGET)    rhiUsage |= (uint32_t)rhi::TextureUsage::RenderTarget;
    if (usage & GPU_TEXTURE_USAGE_DEPTH_STENCIL)    rhiUsage |= (uint32_t)rhi::TextureUsage::DepthStencil;
    if (usage & GPU_TEXTURE_USAGE_PRESENT)           rhiUsage |= (uint32_t)rhi::TextureUsage::Present;
    if (usage & GPU_TEXTURE_USAGE_COPY_SOURCE)      rhiUsage |= (uint32_t)rhi::TextureUsage::CopySource;
    if (usage & GPU_TEXTURE_USAGE_COPY_DEST)        rhiUsage |= (uint32_t)rhi::TextureUsage::CopyDestination;
    return rhiUsage;
}

static inline rhi::TextureType gpuTextureTypeToRhi(GpuTextureType type)
{
    switch (type) {
    case GPU_TEXTURE_TYPE_1D:   return rhi::TextureType::Texture1D;
    case GPU_TEXTURE_TYPE_2D:   return rhi::TextureType::Texture2D;
    case GPU_TEXTURE_TYPE_3D:   return rhi::TextureType::Texture3D;
    case GPU_TEXTURE_TYPE_CUBE: return rhi::TextureType::TextureCube;
    default:                    return rhi::TextureType::Texture2D;
    }
}
