# Phase 2: 现代渲染特性 (M4-M6)

> L5 Pipeline + L6 Layout 层级实现，提升渲染效率与异步计算能力。

---

## T2.1 L5 Pipeline - 管线状态管理

### 目标

统一管理 Graphics/Compute/RayTracing 三类管线，支持延迟创建与缓存复用。

### 前置依赖

- T1.1 GpuDevice + Handle Pool
- T1.2 ShaderCompiler
- T1.5 BindlessHeap

### 实现细节

#### 2.1.1 Pipeline 类型体系

**文件**: `src/gpu/pipeline/gpu_pipeline.h`

```c
typedef enum {
    GPU_PIPELINE_TYPE_GRAPHICS   = 0,
    GPU_PIPELINE_TYPE_COMPUTE    = 1,
    GPU_PIPELINE_TYPE_RAYTRACING = 2,
} GpuPipelineType;

typedef struct {
    GpuPipelineType type;
    const char* label;

    GpuShaderBinary vertexShader;
    GpuShaderBinary fragmentShader;
    GpuShaderBinary geometryShader;
    GpuShaderBinary hullShader;
    GpuShaderBinary domainShader;

    uint32_t renderTargetFormat;
    uint32_t depthStencilFormat;
    uint32_t sampleCount;

    bool depthTestEnable;
    bool depthWriteEnable;
    bool blendEnable;

    uint32_t vertexStride;
} GpuGraphicsPipelineDesc;

typedef struct {
    GpuPipelineType type;
    const char* label;
    GpuShaderBinary computeShader;
} GpuComputePipelineDesc;

GpuResult gpuCreateGraphicsPipeline(GpuDevice device, const GpuGraphicsPipelineDesc* desc, GpuPipelineHandle* outPipeline);
GpuResult gpuCreateComputePipeline(GpuDevice device, const GpuComputePipelineDesc* desc, GpuPipelineHandle* outPipeline);
GpuResult gpuDestroyPipeline(GpuDevice device, GpuPipelineHandle pipeline);
```

#### 2.1.2 Pipeline Cache

**文件**: `src/gpu/pipeline/gpu_pipeline_cache.h`

```c
typedef struct GpuPipelineCache_t* GpuPipelineCache;

GpuResult gpuPipelineCacheCreate(GpuDevice device, const char* cachePath, GpuPipelineCache* outCache);
GpuResult gpuPipelineCacheLookup(GpuPipelineCache cache, const uint8_t* key, size_t keySize, GpuPipelineHandle* outPipeline);
GpuResult gpuPipelineCacheStore(GpuPipelineCache cache, const uint8_t* key, size_t keySize, GpuPipelineHandle pipeline);
GpuResult gpuPipelineCacheFlush(GpuPipelineCache cache);
void gpuPipelineCacheDestroy(GpuPipelineCache cache);
```

内部基于 SHA-256 哈希 pipeline state desc 作为缓存键，磁盘持久化到 `cachePath`。

#### 2.1.3 命令绑定点

扩展 `gpu_cmd.h`:

```c
void gpuCmdBindPipeline(GpuCommandBuffer cmd, GpuPipelineHandle pipeline);
void gpuCmdBindVertexBuffer(GpuCommandBuffer cmd, uint32_t slot, GpuBufferHandle buffer, uint64_t offset);
void gpuCmdBindIndexBuffer(GpuCommandBuffer cmd, GpuBufferHandle buffer, uint64_t offset, uint32_t format);
void gpuCmdDraw(GpuCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance);
void gpuCmdDrawIndexed(GpuCommandBuffer cmd, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
void gpuCmdDispatch(GpuCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
```

### Example 测试

**文件**: `examples/07_compute_pipeline/main.c`
**Shader**: `examples/07_compute_pipeline/prefix_sum.slang`

```slang
// prefix_sum.slang
StructuredBuffer<float> gInput : register(t0);
RWStructuredBuffer<float> gOutput : register(u0);

[shader("compute")]
[numthreads(256, 1, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID) {
    // 简化的前缀和
    float sum = 0.0;
    for (uint i = 0; i <= tid.x; i++) sum += gInput[i];
    gOutput[tid.x] = sum;
}
```

```c
int main() {
    // 创建 Compute Pipeline
    GpuShaderCompileDesc csDesc = {
        .sourcePath = "prefix_sum.slang",
        .entryPoint = "computeMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderBinary csBinary;
    gpuCompileShader(compiler, &csDesc, &csBinary);

    GpuComputePipelineDesc pipeDesc = {
        .type = GPU_PIPELINE_TYPE_COMPUTE,
        .label = "PrefixSum",
        .computeShader = csBinary,
    };
    GpuPipelineHandle pipeline;
    gpuCreateComputePipeline(device, &pipeDesc, &pipeline);

    // 创建输入/输出缓冲
    float inputData[256];
    for (int i = 0; i < 256; i++) inputData[i] = 1.0f;

    GpuBufferDesc inputDesc = { .size = sizeof(inputData), .usage = GPU_BUFFER_USAGE_STORAGE | GPU_BUFFER_USAGE_UPLOAD };
    GpuBufferHandle inputBuf;
    gpuCreateBuffer(device, &inputDesc, &inputBuf);

    // 录制命令
    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(cmd, pipeline);
    gpuCmdDispatch(cmd, 1, 1, 1);
    gpuEndCommandBuffer(cmd);

    GpuCommandQueue queue;
    gpuGetQueue(device, GPU_QUEUE_TYPE_COMPUTE, &queue);
    gpuQueueSubmit(queue, 1, &cmd);
    gpuQueueWaitIdle(queue);

    // 读取回结果验证
    // ...

    printf("Compute pipeline test passed\n");
    return 0;
}
```

### 验证流程

1. Compute Shader 输出结果与 CPU 参考实现一致
2. Pipeline Cache 首次冷启动编译，二次启动命中缓存 (编译时间 <10ms)
3. Graphics Pipeline 渲染三角形与 `06_triangle` 结果一致
4. 多 Pipeline 切换绑定顺序无关性验证

---

## T2.2 L6 Layout - 反射驱动参数映射

### 目标

实现从 Slang Reflection 到材质参数块 (ParameterBlock) 的直接映射，消灭手动绑定胶水代码。

### 前置依赖

- T1.3 GpuTypeInfo / ReflectionCache
- T2.1 Pipeline

### 实现细节

#### 2.2.1 ShaderObject C API

**文件**: `src/gpu/layout/gpu_shader_object.h`

```c
typedef struct GpuShaderObject_t* GpuShaderObject;

typedef struct {
    GpuPipelineHandle pipeline;
    const char* entryPoint;
} GpuShaderObjectDesc;

GpuResult gpuCreateShaderObject(GpuDevice device, const GpuShaderObjectDesc* desc, GpuShaderObjectHandle* outHandle);
GpuResult gpuDestroyShaderObject(GpuDevice device, GpuShaderObjectHandle handle);

void gpuShaderObjectSetData(GpuShaderObject obj, const char* fieldName, const void* data, size_t size);
void gpuShaderObjectSetTexture(GpuShaderObject obj, const char* fieldName, GpuTextureHandle texture);
void gpuShaderObjectSetSampler(GpuShaderObject obj, const char* fieldName, uint32_t samplerIndex);
void gpuShaderObjectSetBuffer(GpuShaderObject obj, const char* fieldName, GpuBufferHandle buffer);

void gpuCmdBindShaderObject(GpuCommandBuffer cmd, GpuShaderObjectHandle obj);
```

内部实现:
- 创建时从 ReflectionCache 获取对应 entryPoint 的 `GpuTypeInfo`
- `SetData` 通过字段名查找偏移量，写入内部 staging buffer
- 提交时自动 flush staging buffer 到 GPU，自动分配描述符

#### 2.2.2 ParameterBlock 系统

**文件**: `src/gpu/layout/gpu_parameter_block.h`

```c
typedef struct GpuParameterBlock_t* GpuParameterBlock;

GpuResult gpuCreateParameterBlock(GpuDevice device, const GpuTypeInfo* typeInfo, GpuParameterBlock* outBlock);
void gpuParameterBlockSetFloat(GpuParameterBlock block, const char* fieldName, float value);
void gpuParameterBlockSetFloat3(GpuParameterBlock block, const char* fieldName, const float value[3]);
void gpuParameterBlockSetInt(GpuParameterBlock block, const char* fieldName, int32_t value);
void gpuParameterBlockSetTexture(GpuParameterBlock block, const char* fieldName, GpuTextureHandle texture);
GpuBufferHandle gpuParameterBlockGetGpuBuffer(GpuParameterBlock block);
void gpuDestroyParameterBlock(GpuParameterBlock block);
```

#### 2.2.3 嵌套 ParameterBlock 支持

```c
GpuResult gpuParameterBlockSetSubBlock(GpuParameterBlock parent, const char* fieldName, GpuParameterBlock child);
```

内部: 递归计算嵌套偏移量，子 block 的 GPU buffer 作为 root descriptor 绑定到父 block 的对应槽位。

### Example 测试

**文件**: `examples/08_material_system/main.c`
**Shader**: `examples/08_material_system/pbr_material.slang`

```slang
// pbr_material.slang
struct Light {
    float3 direction;
    float3 color;
    float intensity;
};

struct MaterialParams {
    float3 baseColor;
    float roughness;
    float metallic;
    int baseColorTex;
    int normalTex;
};

struct SceneParams {
    Light lights[4];
    float3 cameraPos;
    ParameterBlock<MaterialParams> material;
};

ParameterBlock<SceneParams> gScene;

[shader("fragment")]
float4 fragmentMain(float2 uv : TEXCOORD0, float3 normal : NORMAL) : SV_Target {
    MaterialParams mat = gScene.material;
    // ... PBR 计算
    return float4(mat.baseColor, 1.0);
}
```

```c
int main() {
    // 反射获取类型信息
    GpuTypeInfo* sceneTypeInfo;
    gpuReflectShader(reflCache, compiler, "pbr_material.slang", "fragmentMain", &sceneTypeInfo);

    // 创建 ParameterBlock
    GpuParameterBlock sceneBlock;
    gpuCreateParameterBlock(device, sceneTypeInfo, &sceneBlock);

    // 找到嵌套的 MaterialParams
    GpuTypeInfo* materialTypeInfo = findSubTypeInfo(sceneTypeInfo, "material");
    GpuParameterBlock materialBlock;
    gpuCreateParameterBlock(device, materialTypeInfo, &materialBlock);

    // 设置材质参数 (通过字段名自动映射偏移)
    gpuParameterBlockSetFloat3(materialBlock, "baseColor", (float[]){0.8f, 0.2f, 0.1f});
    gpuParameterBlockSetFloat(materialBlock, "roughness", 0.5f);
    gpuParameterBlockSetFloat(materialBlock, "metallic", 0.9f);
    gpuParameterBlockSetInt(materialBlock, "baseColorTex", bindlessAlbedoIdx);
    gpuParameterBlockSetInt(materialBlock, "normalTex", bindlessNormalIdx);

    // 嵌套绑定
    gpuParameterBlockSetSubBlock(sceneBlock, "material", materialBlock);

    // 设置场景参数
    gpuParameterBlockSetFloat3(sceneBlock, "cameraPos", (float[]){0,0,5});

    // 自动提交
    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(cmd, pbrPipeline);
    gpuCmdBindShaderObject(cmd, sceneBlockHandle);
    gpuCmdDraw(cmd, 3, 1);
    gpuEndCommandBuffer(cmd);

    printf("Material system test passed\n");
    return 0;
}
```

### 验证流程

1. `SetFloat3("baseColor", ...)` 写入偏移正确 (对照反射结果的 offset)
2. 嵌套 ParameterBlock 偏移链正确，片段着色器读取值与 CPU 侧一致
3. Bindless 索引通过 `SetInt` 正确传入 shader
4. 修改材质参数后重渲染，颜色变化正确

---

## T2.3 Mesh Shader 支持

### 目标

实现 `IMeshPipeline` 接口，适配现代几何流水线。

### 前置依赖

- T2.1 Pipeline
- T1.6 GpuCapabilities (需 supportMeshShader == true)

### 实现细节

**文件**: `src/gpu/pipeline/gpu_mesh_pipeline.h`

```c
typedef struct {
    GpuPipelineType type;
    const char* label;
    GpuShaderBinary amplificationShader;
    GpuShaderBinary meshShader;
    GpuShaderBinary fragmentShader;
    uint32_t renderTargetFormat;
    uint32_t depthStencilFormat;
} GpuMeshPipelineDesc;

GpuResult gpuCreateMeshPipeline(GpuDevice device, const GpuMeshPipelineDesc* desc, GpuPipelineHandle* outPipeline);
void gpuCmdDrawMeshTasks(GpuCommandBuffer cmd, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);
```

内部: 仅在 `supportMeshShader == true` 时可用，否则返回 `GPU_ERROR_NOT_SUPPORTED`。

### Example 测试

**文件**: `examples/09_mesh_shader/main.c`
**Shader**: `examples/09_mesh_shader/mesh_tri.slang`

```slang
struct MeshPayload {
    uint vertexCount;
};

[shader("amplification")]
[numthreads(1,1,1)]
void amplificationMain(uint3 tid : SV_DispatchThreadID,
                       out Payload<MeshPayload> payload) {
    MeshPayload p;
    p.vertexCount = 3;
    payload = p;
}

[shader("mesh")]
[numthreads(3,1,1)]
[outputtopology("triangle")]
void meshMain(uint tid : SV_DispatchThreadID,
              in Payload<MeshPayload> payload,
              out vertices float3 verts[3],
              out indices uint3 idx[1]) {
    verts[tid] = float3(/* triangle vertices */);
    idx[0] = uint3(0, 1, 2);
}

[shader("fragment")]
float4 fragmentMain(float3 pos : POSITION) : SV_Target {
    return float4(1, 0, 0, 1);
}
```

```c
int main() {
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    if (!caps.supportMeshShader) {
        printf("Mesh Shader not supported, skipping\n");
        return 0;
    }

    GpuMeshPipelineDesc desc = { /* ... */ };
    GpuPipelineHandle meshPipe;
    gpuCreateMeshPipeline(device, &desc, &meshPipe);

    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(cmd, meshPipe);
    gpuCmdDrawMeshTasks(cmd, 1, 1, 1);
    gpuEndCommandBuffer(cmd);

    printf("Mesh Shader rendered triangle\n");
    return 0;
}
```

### 验证流程

1. Mesh Shader 渲染出红色三角形
2. 不支持的平台优雅降级 (打印 skip 信息，返回 0)
3. Amplification → Mesh → Fragment 数据传递正确

---

## T2.4 Multi Queue 架构

### 目标

Fork 并重构 Slang-RHI 的命令队列模型，引入独立 Compute 和 Transfer 队列，支持异步计算。

### 前置依赖

- T1.1 GpuCommandQueue
- T2.1 Pipeline

### 实现细节

#### 2.4.1 多队列扩展

**文件**: `src/gpu/queue/gpu_multi_queue.h`

```c
typedef struct {
    GpuCommandQueue graphics;
    GpuCommandQueue compute;
    GpuCommandQueue transfer;
} GpuQueueSet;

GpuResult gpuGetQueueSet(GpuDevice device, GpuQueueSet* outSet);

typedef struct GpuFence_t* GpuFence;
GpuResult gpuCreateFence(GpuDevice device, uint64_t initialValue, GpuFence* outFence);
uint64_t gpuFenceGetCompletedValue(GpuFence fence);
GpuResult gpuFenceWait(GpuFence fence, uint64_t value, uint32_t timeoutMs);
void gpuDestroyFence(GpuDevice device, GpuFence fence);
```

#### 2.4.2 跨队列同步

```c
typedef struct {
    GpuCommandQueue signalQueue;
    GpuFence signalFence;
    uint64_t signalValue;
} GpuQueueSemaphore;

GpuResult gpuQueueSubmitWithWait(GpuCommandQueue queue,
                                  uint32_t waitCount,
                                  const GpuQueueSemaphore* waits,
                                  uint32_t cmdCount,
                                  GpuCommandBuffer* cmds,
                                  const GpuQueueSemaphore* signal);
```

### Example 测试

**文件**: `examples/10_async_compute/main.c`

```c
int main() {
    GpuQueueSet queues;
    gpuGetQueueSet(device, &queues);

    GpuFence computeFence;
    gpuCreateFence(device, 0, &computeFence);

    // 异步计算: 前缀和
    GpuCommandBuffer computeCmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(computeCmd, prefixSumPipeline);
    gpuCmdDispatch(computeCmd, 1, 1, 1);
    gpuEndCommandBuffer(computeCmd);

    GpuQueueSemaphore computeSignal = { .signalQueue = queues.compute, .signalFence = computeFence, .signalValue = 1 };
    gpuQueueSubmitWithWait(queues.compute, 0, NULL, 1, &computeCmd, &computeSignal);

    // 图形渲染: 等待计算完成后使用结果
    GpuCommandBuffer gfxCmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(gfxCmd, renderPipeline);
    gpuCmdDraw(gfxCmd, 3, 1);
    gpuEndCommandBuffer(gfxCmd);

    GpuQueueSemaphore computeWait = { .signalQueue = queues.compute, .signalFence = computeFence, .signalValue = 1 };
    gpuQueueSubmitWithWait(queues.graphics, 1, &computeWait, 1, &gfxCmd, NULL);

    gpuFenceWait(computeFence, 1, 5000);
    printf("Async compute + graphics pipeline completed\n");
    return 0;
}
```

### 验证流程

1. Compute 队列独立执行，不阻塞 Graphics 队列
2. Fence 信号正确传递，GPU 端等待后数据一致
3. 对比单队列 vs 多队列: 异步计算场景性能提升 >15%
4. Transfer 队列独立上传纹理数据成功

---

## T2.5 Sparse Resource 支持

### 目标

实现稀疏资源与虚拟贴图技术，支持超大规模场景的资源流送。

### 前置依赖

- T1.4 StreamingManager
- T1.6 GpuCapabilities (需 supportSparseResource == true)

### 实现细节

**文件**: `src/gpu/resource/gpu_sparse_resource.h`

```c
typedef struct {
    GpuTextureHandle texture;
    uint32_t mipLevel;
    uint32_t tileX, tileY, tileZ;
} GpuSparseTileCoord;

GpuResult gpuSparseReserve(GpuDevice device, GpuTextureHandle texture, uint32_t tileCount, GpuSparseTileCoord* tiles);
GpuResult gpuSparseMap(GpuDevice device, GpuTextureHandle texture, const GpuSparseTileCoord* tile, GpuBufferHandle backingMemory, uint64_t offset);
GpuResult gpuSparseUnmap(GpuDevice device, GpuTextureHandle texture, const GpuSparseTileCoord* tile);
```

### Example 测试

**文件**: `examples/11_sparse_texture/main.c`

```c
int main() {
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    if (!caps.supportSparseResource) {
        printf("Sparse resource not supported, skipping\n");
        return 0;
    }

    // 创建稀疏纹理 (8192x8192, 8 mips)
    GpuTextureDesc texDesc = { .width = 8192, .height = 8192, .mipCount = 8, .format = GPU_FORMAT_RGBA8, .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_SPARSE };
    GpuTextureHandle sparseTex;
    gpuCreateTexture(device, &texDesc, &sparseTex);

    // 只驻留 mip 0-3 的 tile
    GpuSparseTileCoord tiles[16];
    for (int mip = 0; mip < 4; mip++) {
        tiles[mip] = (GpuSparseTileCoord){ .texture = sparseTex, .mipLevel = mip, .tileX = 0, .tileY = 0, .tileZ = 0 };
    }
    gpuSparseReserve(device, sparseTex, 4, tiles);

    // 将 tile 映射到 backing memory
    gpuSparseMap(device, sparseTex, &tiles[0], backingBuffer, 0);

    printf("Sparse texture mapped successfully\n");
    return 0;
}
```

### 验证流程

1. 创建大尺寸稀疏纹理，仅映射部分 tile
2. 采样已映射 tile 区域颜色正确
3. 采样未映射 tile 区域返回零/默认值
4. 动态映射/解映射 tile 不崩溃
5. 不支持平台返回 `GPU_ERROR_NOT_SUPPORTED` 并跳过

---

## Phase 2 集成测试

### PBR 材质渲染 Example

**文件**: `examples/12_pbr_material/main.c`

整合 T2.1 ~ T2.5 所有模块:

```c
int main() {
    // 1. 初始化
    gpuCreateDevice(&devDesc, &device);
    GpuQueueSet queues;
    gpuGetQueueSet(device, &queues);

    // 2. 编译 PBR Shader 并反射
    gpuCompileShader(compiler, &pbrShaderDesc, &pbrBinary);
    gpuReflectShader(reflCache, compiler, "pbr.slang", "fragmentMain", &pbrTypeInfo);

    // 3. 创建 Pipeline + ParameterBlock
    gpuCreateGraphicsPipeline(device, &pbrPipeDesc, &pbrPipeline);
    gpuCreateParameterBlock(device, pbrTypeInfo, &sceneBlock);

    // 4. 配置材质参数 (自动映射)
    gpuParameterBlockSetFloat3(materialBlock, "baseColor", ...);
    gpuParameterBlockSetFloat(materialBlock, "roughness", 0.5f);
    gpuParameterBlockSetTexture(materialBlock, "albedoMap", albedoTex);
    gpuParameterBlockSetSubBlock(sceneBlock, "material", materialBlock);

    // 5. 异步计算队列: 阴影/SSAO
    GpuCommandBuffer shadowCmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(shadowCmd, shadowPipeline);
    gpuCmdDispatch(shadowCmd, ...);
    gpuEndCommandBuffer(shadowCmd);

    GpuQueueSemaphore shadowDone = { ... };
    gpuQueueSubmitWithWait(queues.compute, 0, NULL, 1, &shadowCmd, &shadowDone);

    // 6. 图形队列渲染 (等待异步计算)
    GpuCommandBuffer gfxCmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(gfxCmd, pbrPipeline);
    gpuCmdBindShaderObject(gfxCmd, sceneBlockHandle);
    gpuCmdDrawIndexed(gfxCmd, indexCount, 1, 0, 0, 0);
    gpuEndCommandBuffer(gfxCmd);

    gpuQueueSubmitWithWait(queues.graphics, 1, &shadowDone, 1, &gfxCmd, NULL);

    printf("PBR material rendered with async compute\n");
    return 0;
}
```

### Phase 2 里程碑验收

| 验收项 | 标准方法 |
|:---|:---|
| ShaderObject 自动绑定 | T2.2 通过字段名 set/get 数据，GPU 读取一致 |
| 嵌套 ParameterBlock | T2.2 嵌套 block 偏移链正确，渲染结果正确 |
| Compute Pipeline | T2.1 前缀和输出与 CPU 参考一致 |
| Mesh Shader 渲染 | T2.3 三角形正确渲染，不支持平台优雅跳过 |
| 异步计算性能 | T2.4 多队列 vs 单队列性能提升 >15% |
| Sparse 有效 | T2.5 稀疏纹理映射/解映射正常工作 |
