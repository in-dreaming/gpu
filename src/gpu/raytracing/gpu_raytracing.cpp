#include "gpu/raytracing/gpu_raytracing.h"
#include "gpu/core/gpu_buffer.h"
#include "gpu/core/gpu_internal.h"

GpuResult gpuCreateBottomLevelAS(GpuDevice device, const GpuBottomLevelASDesc* desc, GpuAccelerationStructureHandle* outAS)
{
    if (!device || !desc || !outAS) return GPU_ERROR_INVALID_ARGS;
    if (!desc->geometryCount || !desc->geometries) return GPU_ERROR_INVALID_ARGS;

    // Check if ray tracing is supported
    if (!device->rhiDevice->hasFeature(rhi::Feature::AccelerationStructure)) {
        return GPU_ERROR_NOT_SUPPORTED;
    }

    // Build the acceleration structure geometry descriptions
    std::vector<rhi::AccelerationStructureBuildInput> buildInputs;
    std::vector<rhi::BufferOffsetPair> vertexBuffers;
    std::vector<rhi::BufferOffsetPair> indexBuffers;

    for (uint32_t i = 0; i < desc->geometryCount; i++) {
        const GpuAccelerationStructureGeometry& geom = desc->geometries[i];
        
        // Get vertex buffer
        rhi::IBuffer* rhiVertexBuffer = nullptr;
        if (gpuHandleIsValid(geom.vertexBuffer)) {
            rhiVertexBuffer = device->bufferPool.resolve(geom.vertexBuffer.index, geom.vertexBuffer.generation);
        }
        
        // Get index buffer
        rhi::IBuffer* rhiIndexBuffer = nullptr;
        if (gpuHandleIsValid(geom.indexBuffer)) {
            rhiIndexBuffer = device->bufferPool.resolve(geom.indexBuffer.index, geom.indexBuffer.generation);
        }

        rhi::AccelerationStructureBuildInput input = {};
        input.type = rhi::AccelerationStructureBuildInputType::Triangles;
        input.triangles.vertexBuffers[0] = rhi::BufferOffsetPair(rhiVertexBuffer, 0);
        input.triangles.vertexBufferCount = rhiVertexBuffer ? 1 : 0;
        input.triangles.vertexFormat = gpuFormatToRhi(geom.vertexFormat);
        input.triangles.vertexCount = geom.triangleCount * 3;
        input.triangles.vertexStride = geom.vertexStride;
        input.triangles.indexBuffer = rhi::BufferOffsetPair(rhiIndexBuffer, 0);
        input.triangles.indexFormat = rhi::IndexFormat::Uint32;
        input.triangles.indexCount = rhiIndexBuffer ? geom.triangleCount * 3 : 0;
        
        buildInputs.push_back(input);
    }

    // Build desc
    rhi::AccelerationStructureBuildDesc buildDesc = {};
    buildDesc.inputs = buildInputs.data();
    buildDesc.inputCount = (uint32_t)buildInputs.size();
    buildDesc.mode = rhi::AccelerationStructureBuildMode::Build;
    buildDesc.flags = desc->allowUpdate ? rhi::AccelerationStructureBuildFlags::AllowUpdate : rhi::AccelerationStructureBuildFlags::None;

    // Get required sizes
    rhi::AccelerationStructureSizes sizes;
    rhi::Result r = device->rhiDevice->getAccelerationStructureSizes(buildDesc, &sizes);
    if (SLANG_FAILED(r)) {
        return GPU_ERROR_INTERNAL;
    }

    // Create acceleration structure
    rhi::AccelerationStructureDesc asDesc = {};
    asDesc.kind = rhi::AccelerationStructureKind::BottomLevel;
    asDesc.size = sizes.accelerationStructureSize;
    asDesc.flags = buildDesc.flags;

    rhi::ComPtr<rhi::IAccelerationStructure> rhiAS;
    r = device->rhiDevice->createAccelerationStructure(asDesc, rhiAS.writeRef());
    if (SLANG_FAILED(r)) {
        return GPU_ERROR_INTERNAL;
    }

    // Allocate handle
    uint32_t idx = device->accelStructPool.allocate(rhiAS.detach());
    if (idx == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outAS->index = idx;
    outAS->generation = device->accelStructPool.slots[idx].generation;

    // Note: Build happens in gpuBuildAccelerationStructure

    return GPU_SUCCESS;
}

GpuResult gpuCreateTopLevelAS(GpuDevice device, const GpuTopLevelASDesc* desc, GpuAccelerationStructureHandle* outAS)
{
    if (!device || !desc || !outAS) return GPU_ERROR_INVALID_ARGS;

    // Check if ray tracing is supported
    if (!device->rhiDevice->hasFeature(rhi::Feature::AccelerationStructure)) {
        return GPU_ERROR_NOT_SUPPORTED;
    }

    // Get instance buffer
    rhi::IBuffer* rhiInstanceBuffer = nullptr;
    if (gpuHandleIsValid(desc->instanceBuffer)) {
        rhiInstanceBuffer = device->bufferPool.resolve(desc->instanceBuffer.index, desc->instanceBuffer.generation);
    }

    // Build desc for TLAS
    rhi::AccelerationStructureBuildDesc buildDesc = {};
    rhi::AccelerationStructureBuildInput input = {};
    input.type = rhi::AccelerationStructureBuildInputType::Instances;
    input.instances.instanceBuffer = rhi::BufferOffsetPair(rhiInstanceBuffer, 0);
    input.instances.instanceCount = desc->instanceCount;
    input.instances.instanceStride = sizeof(rhi::AccelerationStructureInstanceDescGeneric);
    
    buildDesc.inputs = &input;
    buildDesc.inputCount = 1;
    buildDesc.mode = rhi::AccelerationStructureBuildMode::Build;

    // Get required sizes
    rhi::AccelerationStructureSizes sizes;
    rhi::Result r = device->rhiDevice->getAccelerationStructureSizes(buildDesc, &sizes);
    if (SLANG_FAILED(r)) {
        return GPU_ERROR_INTERNAL;
    }

    // Create acceleration structure
    rhi::AccelerationStructureDesc asDesc = {};
    asDesc.kind = rhi::AccelerationStructureKind::TopLevel;
    asDesc.size = sizes.accelerationStructureSize;

    rhi::ComPtr<rhi::IAccelerationStructure> rhiAS;
    r = device->rhiDevice->createAccelerationStructure(asDesc, rhiAS.writeRef());
    if (SLANG_FAILED(r)) {
        return GPU_ERROR_INTERNAL;
    }

    // Allocate handle
    uint32_t idx = device->accelStructPool.allocate(rhiAS.detach());
    if (idx == 0) {
        return GPU_ERROR_OUT_OF_MEMORY;
    }

    outAS->index = idx;
    outAS->generation = device->accelStructPool.slots[idx].generation;

    return GPU_SUCCESS;
}

GpuResult gpuBuildAccelerationStructure(GpuCommandBuffer cmd, GpuAccelerationStructureHandle as, bool update)
{
    if (!cmd || !as.index) return GPU_ERROR_INVALID_ARGS;

    rhi::IAccelerationStructure* rhiAS = cmd->device->accelStructPool.resolve(as.index, as.generation);
    if (!rhiAS) return GPU_ERROR_INVALID_ARGS;

    (void)update;
    return GPU_ERROR_NOT_SUPPORTED;
}

GpuResult gpuDestroyAccelerationStructure(GpuDevice device, GpuAccelerationStructureHandle as)
{
    if (!device || !as.index) return GPU_SUCCESS;

    rhi::IAccelerationStructure* rhiAS = device->accelStructPool.resolve(as.index, as.generation);
    if (!rhiAS) return GPU_ERROR_INVALID_ARGS;

    rhiAS->release();
    device->accelStructPool.release(as.index, as.generation);
    return GPU_SUCCESS;
}

GpuResult gpuCreateRayTracingPipeline(GpuDevice device, const GpuRayTracingPipelineDesc* desc, GpuPipelineHandle* outPipeline)
{
    if (!device || !desc || !outPipeline) return GPU_ERROR_INVALID_ARGS;

    if (!device->rhiDevice->hasFeature(rhi::Feature::RayTracing)) {
        return GPU_ERROR_NOT_SUPPORTED;
    }

    *outPipeline = GPU_NULL_HANDLE;
    return GPU_ERROR_NOT_SUPPORTED;
}

void gpuCmdTraceRays(GpuCommandBuffer cmd, GpuPipelineHandle rtPipeline, uint32_t width, uint32_t height, uint32_t depth)
{
    (void)cmd;
    (void)rtPipeline;
    (void)width;
    (void)height;
    (void)depth;
    // Would use command buffer to dispatch ray tracing
}
