# Phase 3: AI 与前沿技术 (M7-M12)

> L7 Backend + L8 Capability + L9 Future AI 层级实现，补齐 AI 推理加速与 GPU 驱动任务调度能力。

---

## T3.1 L7 Backend - 多后端驱动适配

### 目标

适配 Slang-RHI 及原生 API 后端，实现各平台差异化特性的正确调度。

### 前置依赖

- Phase 1 所有任务 (L0-L4 已可用)
- Phase 2 所有任务 (L5-L6 已可用)

### 实现细节

#### 3.1.1 Backend 抽象接口

**文件**: `src/gpu/backend/gpu_backend.h`

```c
typedef enum {
    GPU_BACKEND_VULKAN = 0,
    GPU_BACKEND_D3D12  = 1,
    GPU_BACKEND_METAL  = 2,
    GPU_BACKEND_WEBGPU = 3,
} GpuBackendType;

typedef struct GpuBackend_vtbl {
    GpuResult (*createDevice)(const GpuDeviceDesc* desc, GpuDevice* outDevice);
    GpuResult (*createBuffer)(GpuDevice device, const GpuBufferDesc* desc, GpuBufferHandle* outHandle);
    GpuResult (*createTexture)(GpuDevice device, const GpuTextureDesc* desc, GpuTextureHandle* outHandle);
    GpuResult (*createPipeline)(GpuDevice device, const void* desc, GpuPipelineType type, GpuPipelineHandle* outHandle);
    GpuResult (*createCommandBuffer)(GpuDevice device, GpuCommandBuffer* outCmd);
    GpuResult (*queueSubmit)(GpuCommandQueue queue, uint32_t count, GpuCommandBuffer* cmds);
    void (*destroyDevice)(GpuDevice device);
} GpuBackend_vtbl;

GpuResult gpuBackendCreate(GpuBackendType type, const GpuBackend_vtbl** outVtbl);
```

#### 3.1.2 Vulkan Backend (首选成熟后端)

**文件**: `src/gpu/backend/vulkan/gpu_vulkan_backend.cpp`

关键实现点:
- 集成 VMA 子分配策略
- Bindless: `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT` + `VK_EXT_descriptor_indexing`
- Mesh Shader: `VK_EXT_mesh_shader`
- Ray Tracing: `VK_KHR_ray_tracing_pipeline`
- Fence 同步: `VkFence` + `vkWaitForFences`

验证:
- 所有 Phase 1/2 的 example 在 Vulkan 后端上跑通
- Bindless 索引 > 10000 不出错

#### 3.1.3 D3D12 Backend

**文件**: `src/gpu/backend/d3d12/gpu_d3d12_backend.cpp`

关键实现点:
- 集成 D3D12MA 子分配策略
- Bindless: Shader-visible CBV_SRV_UAV descriptor heap
- Work Graphs: `ID3D12WorkGraph` (Agility SDK)
- Fence 同步: `ID3D12Fence` + `SetEventOnCompletion`

验证:
- 所有 example 在 D3D12 后端上跑通
- Work Graphs demo 运行正确

#### 3.1.4 Metal Backend

**文件**: `src/gpu/backend/metal/gpu_metal_backend.mm`

关键实现点:
- Bindless 模拟: Argument Buffers (`MTLArgumentEncoder`)
- Tile-based 渲染优化: `MTLTileRenderPipelineDescriptor`
- 编码器状态缓存减少 `set*` 调用开销

验证:
- 基础三角形/Compute Shader 在 Metal 后端跑通
- Argument Buffer 绑定正确采样纹理

#### 3.1.5 WebGPU Backend

**文件**: `src/gpu/backend/webgpu/gpu_webgpu_backend.cpp`

关键实现点:
- 基于 Dawn/C++ WebGPU API
- Bindless 降级: 固定绑定槽位 + storage buffer 索引数组
- 射线追踪不可用: 返回 `GPU_ERROR_NOT_SUPPORTED`
- 优先保证 Tensor/Compute 接口兼容

验证:
- Compute Shader 在 WebGPU 后端跑通
- 不支持的特性优雅降级

### Example 测试

**文件**: `examples/13_backend_switch/main.c`

```c
int main(int argc, char** argv) {
    GpuBackendType backend = GPU_BACKEND_VULKAN;
    if (argc > 1) {
        if (strcmp(argv[1], "d3d12") == 0)  backend = GPU_BACKEND_D3D12;
        if (strcmp(argv[1], "metal") == 0)  backend = GPU_BACKEND_METAL;
        if (strcmp(argv[1], "webgpu") == 0) backend = GPU_BACKEND_WEBGPU;
    }

    const GpuBackend_vtbl* vtbl;
    gpuBackendCreate(backend, &vtbl);

    GpuDevice device;
    vtbl->createDevice(&(GpuDeviceDesc){.appName = "backend_test"}, &device);

    // 运行标准渲染测试
    // ...

    const char* names[] = { "Vulkan", "D3D12", "Metal", "WebGPU" };
    printf("Backend %s test passed\n", names[backend]);
    return 0;
}
```

### 验证流程

1. 同一份 example 代码，通过参数切换后端均跑通
2. 各后端渲染结果视觉一致（允许浮点精度差异）
3. 不支持的功能在各后端正确返回 `GPU_ERROR_NOT_SUPPORTED`
4. 跨后端切换无需重编译，仅运行时选择

---

## T3.2 L8 Capability - 功能特性对齐系统

### 目标

构建独立的 Feature Gating 系统，确保同一套代码在不同硬件/后端条件下安全运行。

### 前置依赖

- T1.6 GpuCapabilities 基础框架
- T3.1 多后端适配

### 实现细节

**文件**: `src/gpu/capability/gpu_feature_gating.h`

```c
typedef enum {
    GPU_FEATURE_BINDLESS         = 0,
    GPU_FEATURE_MESH_SHADER      = 1,
    GPU_FEATURE_RAY_TRACING      = 2,
    GPU_FEATURE_WORK_GRAPH       = 3,
    GPU_FEATURE_COOPERATIVE_MATRIX = 4,
    GPU_FEATURE_SPARSE_RESOURCE  = 5,
    GPU_FEATURE_ASYNC_COMPUTE    = 6,
    GPU_FEATURE_COUNT,
} GpuFeature;

typedef struct {
    bool supported[GPU_FEATURE_COUNT];
    uint32_t limits[GPU_FEATURE_COUNT];
} GpuFeatureTable;

GpuResult gpuBuildFeatureTable(GpuDevice device, GpuFeatureTable* outTable);
bool gpuIsFeatureSupported(const GpuFeatureTable* table, GpuFeature feature);
uint32_t gpuGetFeatureLimit(const GpuFeatureTable* table, GpuFeature feature);
```

**文件**: `src/gpu/capability/gpu_fallback_registry.h`

特性降级注册表: 定义当某特性不支持时使用的替代路径。

```c
typedef struct {
    GpuFeature feature;
    const char* fallbackShaderPath;
    GpuPipelineType fallbackPipelineType;
} GpuFallbackEntry;

GpuResult gpuRegisterFallback(GpuDevice device, const GpuFallbackEntry* entry);
GpuResult gpuResolveFallback(GpuDevice device, GpuFeature requiredFeature,
                              const char** outShaderPath, GpuPipelineType* outType);
```

### Example 测试

**文件**: `examples/14_feature_gating/main.c`

```c
int main() {
    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);

    printf("Feature Matrix:\n");
    printf("  Bindless:         %s\n", features.supported[GPU_FEATURE_BINDLESS] ? "YES" : "NO");
    printf("  Mesh Shader:      %s\n", features.supported[GPU_FEATURE_MESH_SHADER] ? "YES" : "NO");
    printf("  Ray Tracing:      %s\n", features.supported[GPU_FEATURE_RAY_TRACING] ? "YES" : "NO");
    printf("  Cooperative Matrix: %s\n", features.supported[GPU_FEATURE_COOPERATIVE_MATRIX] ? "YES" : "NO");

    if (!features.supported[GPU_FEATURE_MESH_SHADER]) {
        const char* fallbackPath;
        GpuPipelineType fallbackType;
        gpuResolveFallback(device, GPU_FEATURE_MESH_SHADER, &fallbackPath, &fallbackType);
        printf("Mesh Shader fallback: %s (type=%d)\n", fallbackPath, fallbackType);
    }

    return 0;
}
```

### 验证流程

1. 不同后端返回不同 Feature Table (Vulkan 最全, WebGPU 最少)
2. 使用 Mesh Shader fallbak 路径渲染，结果与直接 Mesh Shader 视觉一致
3. 所有 Feature 均有对应 fallback 或明确的错误返回
4. Feature Table 可序列化/反序列化 (JSON 格式)

---

## T3.3 L9 Future AI - 神经计算接口

### 目标

预留 Tensor 和矩阵计算接口，为神经渲染与 AI 推理加速提供统一基础设施。

### 前置依赖

- T3.1 多后端适配
- T3.2 Feature Gating (需 `GPU_FEATURE_COOPERATIVE_MATRIX`)
- T2.1 Compute Pipeline

### 实现细节

#### 3.3.1 GpuTensor 类型

**文件**: `src/gpu/ai/gpu_tensor.h`

```c
typedef enum {
    GPU_TENSOR_FORMAT_F32 = 0,
    GPU_TENSOR_FORMAT_F16 = 1,
    GPU_TENSOR_FORMAT_BF16 = 2,
    GPU_TENSOR_FORMAT_I8  = 3,
    GPU_TENSOR_FORMAT_U8  = 4,
} GpuTensorFormat;

typedef struct {
    GpuTensorFormat format;
    uint32_t dimCount;
    uint32_t dims[4];
    uint32_t strides[4];
} GpuTensorDesc;

GpuResult gpuCreateTensor(GpuDevice device, const GpuTensorDesc* desc, GpuHandle* outHandle);
GpuResult gpuDestroyTensor(GpuDevice device, GpuHandle tensor);
GpuBufferHandle gpuGetTensorBuffer(GpuDevice device, GpuHandle tensor);
uint64_t gpuGetTensorBufferSize(GpuDevice device, GpuHandle tensor);
```

#### 3.3.2 GpuMatrixPipeline

**文件**: `src/gpu/ai/gpu_matrix_pipeline.h`

```c
typedef struct {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    GpuTensorFormat aFormat;
    GpuTensorFormat bFormat;
    GpuTensorFormat cFormat;
    bool useCooperativeMatrix;
} GpuMatmulDesc;

GpuResult gpuCreateMatmulPipeline(GpuDevice device, const GpuMatmulDesc* desc, GpuPipelineHandle* outPipeline);

typedef struct {
    GpuHandle a;
    GpuHandle b;
    GpuHandle c;
} GpuMatmulBindings;

void gpuCmdMatmul(GpuCommandBuffer cmd, GpuPipelineHandle matmulPipeline, const GpuMatmulBindings* bindings);
```

内部:
- `useCooperativeMatrix=true` 时生成使用 `GLSL_cooperative_matrix` / `VK_KHR_cooperative_matrix` 的 shader
- `useCooperativeMatrix=false` 时使用 tiled matmul compute shader fallback

#### 3.3.3 神经渲染辅助接口

**文件**: `src/gpu/ai/gpu_neural.h`

```c
typedef struct {
    uint32_t inputWidth;
    uint32_t inputHeight;
    uint32_t hiddenDim;
    uint32_t outputChannels;
    GpuTensorFormat weightFormat;
} GpuNeuralNetworkDesc;

GpuResult gpuCreateNeuralNetwork(GpuDevice device, const GpuNeuralNetworkDesc* desc, GpuHandle* outNetwork);
GpuResult gpuNeuralNetworkInference(GpuCommandBuffer cmd, GpuHandle network, GpuHandle input, GpuHandle output);
GpuResult gpuLoadNeuralNetworkWeights(GpuDevice device, GpuHandle network, const void* weightData, size_t weightSize);
void gpuDestroyNeuralNetwork(GpuDevice device, GpuHandle network);
```

### Example 测试

**文件**: `examples/15_tensor_matmul/main.c`
**Shader**: `examples/15_tensor_matmul/matmul.slang`

```slang
// matmul.slang - 使用 Cooperative Matrix
#if USE_COOP_MATRIX
using namespace coopmat;

[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID) {
    cooperative_matrix<float, CooperativeMatrixGroupScope_Subgroup, 16, 16, CooperativeMatrixMatrixUse_MatrixA> matA;
    cooperative_matrix<float, CooperativeMatrixGroupScope_Subgroup, 16, 16, CooperativeMatrixMatrixUse_MatrixB> matB;
    cooperative_matrix<float, CooperativeMatrixGroupScope_Subgroup, 16, 16, CooperativeMatrixMatrixUse_Accumulator> matC;

    coopMatrixLoad(matA, gA, ...);
    coopMatrixLoad(matB, gB, ...);
    matC = coopMatrixMulAdd(matA, matB, matC);
    coopMatrixStore(matC, gC, ...);
}
#else
[shader("compute")]
[numthreads(16, 16, 1)]
void computeMain(uint3 tid : SV_DispatchThreadID) {
    // Tiled matmul fallback
}
#endif
```

```c
int main() {
    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);

    // 创建 Tensor
    GpuTensorDesc aDesc = { .format = GPU_TENSOR_FORMAT_F16, .dimCount = 2, .dims = {1024, 1024} };
    GpuHandle tensorA, tensorB, tensorC;
    gpuCreateTensor(device, &aDesc, &tensorA);
    gpuCreateTensor(device, &aDesc, &tensorB);
    GpuTensorDesc cDesc = { .format = GPU_TENSOR_FORMAT_F32, .dimCount = 2, .dims = {1024, 1024} };
    gpuCreateTensor(device, &cDesc, &tensorC);

    // 创建 Matmul Pipeline
    bool useCoop = features.supported[GPU_FEATURE_COOPERATIVE_MATRIX];
    GpuMatmulDesc matmulDesc = {
        .m = 1024, .n = 1024, .k = 1024,
        .aFormat = GPU_TENSOR_FORMAT_F16,
        .bFormat = GPU_TENSOR_FORMAT_F16,
        .cFormat = GPU_TENSOR_FORMAT_F32,
        .useCooperativeMatrix = useCoop,
    };
    GpuPipelineHandle matmulPipe;
    gpuCreateMatmulPipeline(device, &matmulDesc, &matmulPipe);

    // 执行
    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
    GpuMatmulBindings bindings = { .a = tensorA, .b = tensorB, .c = tensorC };
    gpuCmdMatmul(cmd, matmulPipe, &bindings);
    gpuEndCommandBuffer(cmd);

    gpuQueueSubmit(queue, 1, &cmd);
    gpuQueueWaitIdle(queue);

    printf("Matmul 1024x1024 completed (coop_matrix=%s)\n", useCoop ? "YES" : "NO");
    return 0;
}
```

**文件**: `examples/16_neural_render/main.c` - 神经渲染 Demo

```c
int main() {
    GpuNeuralNetworkDesc nnDesc = {
        .inputWidth = 256,
        .inputHeight = 256,
        .hiddenDim = 64,
        .outputChannels = 3,
        .weightFormat = GPU_TENSOR_FORMAT_F16,
    };
    GpuHandle neuralNet;
    gpuCreateNeuralNetwork(device, &nnDesc, &neuralNet);

    // 加载预训练权重
    void* weights = loadFile("nerf_weights.bin");
    gpuLoadNeuralNetworkWeights(device, neuralNet, weights, weightSize);

    // 推理
    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
    gpuNeuralNetworkInference(cmd, neuralNet, inputTensor, outputTensor);
    gpuEndCommandBuffer(cmd);

    gpuQueueSubmit(queue, 1, &cmd);
    gpuQueueWaitIdle(queue);

    printf("Neural rendering inference completed\n");
    return 0;
}
```

### 验证流程

1. Matmul 结果与 CPU 参考实现 (如 BLAS) 差异 < 1e-3 (F32) / < 1e-2 (F16)
2. Cooperative Matrix 路径 vs Tiled fallback 路径精度均在误差范围内
3. Tensor 创建/销毁生命周期正确，无泄漏
4. 神经网络推理输出维度正确，权重加载后输出非平凡
5. WebGPU 后端 matmul 通过 tiled fallback 正确执行

---

## T3.4 Work Graph 支持

### 目标

引入 GPU Task Graph 机制，实现 GPU 驱动的复杂渲染任务调度。

### 前置依赖

- T3.1 D3D12 Backend (Work Graphs 仅 D3D12 Agility SDK)
- T3.2 Feature Gating (`GPU_FEATURE_WORK_GRAPH`)

### 实现细节

**文件**: `src/gpu/workgraph/gpu_work_graph.h`

```c
typedef struct {
    const char* name;
    GpuShaderBinary nodeShader;
    const char* entryPoint;
    bool isBroadcastNode;
} GpuWorkGraphNodeDesc;

typedef struct {
    uint32_t nodeCount;
    const GpuWorkGraphNodeDesc* nodes;
    uint32_t edgeCount;
    struct { uint32_t from; uint32_t to; }* edges;
} GpuWorkGraphDesc;

typedef struct GpuWorkGraph_t* GpuWorkGraph;

GpuResult gpuCreateWorkGraph(GpuDevice device, const GpuWorkGraphDesc* desc, GpuWorkGraph* outGraph);
GpuResult gpuWorkGraphInitialize(GpuWorkGraph graph, GpuCommandBuffer cmd);
void gpuCmdDispatchWorkGraph(GpuCommandBuffer cmd, GpuWorkGraph graph, uint32_t recordCount);
void gpuDestroyWorkGraph(GpuDevice device, GpuWorkGraph graph);
```

### Example 测试

**文件**: `examples/17_workgraph/main.c`

```c
int main() {
    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);
    if (!features.supported[GPU_FEATURE_WORK_GRAPH]) {
        printf("Work Graph not supported, skipping\n");
        return 0;
    }

    GpuWorkGraphNodeDesc nodes[] = {
        { .name = "Cull", .nodeShader = cullBinary, .entryPoint = "cullNode", .isBroadcastNode = true },
        { .name = "Draw", .nodeShader = drawBinary, .entryPoint = "drawNode", .isBroadcastNode = false },
    };

    struct { uint32_t from; uint32_t to; } edges[] = { {0, 1} };

    GpuWorkGraphDesc wgDesc = { .nodeCount = 2, .nodes = nodes, .edgeCount = 1, .edges = edges };
    GpuWorkGraph wg;
    gpuCreateWorkGraph(device, &wgDesc, &wg);

    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
    gpuWorkGraphInitialize(wg, cmd);
    gpuCmdDispatchWorkGraph(cmd, wg, 1);
    gpuEndCommandBuffer(cmd);

    gpuQueueSubmit(queue, 1, &cmd);
    gpuQueueWaitIdle(queue);

    printf("Work Graph executed: Cull -> Draw\n");
    return 0;
}
```

### 验证流程

1. Work Graph 节点间数据传递正确 (Cull 输出 → Draw 输入)
2. 仅 D3D12 后端支持，其他后端返回 `GPU_ERROR_NOT_SUPPORTED`
3. 多帧稳定运行不崩溃
4. 与传统 CPU Dispatch 方案对比: 减少 CPU→GPU 往返次数

---

## T3.5 Ray Tracing 模块

### 目标

完善光线追踪管线支持，实现跨后端的 RT 接口对齐。

### 前置依赖

- T3.1 Vulkan Backend (VK_KHR_ray_tracing_pipeline)
- T3.2 Feature Gating (`GPU_FEATURE_RAY_TRACING`)

### 实现细节

**文件**: `src/gpu/raytracing/gpu_raytracing.h`

```c
typedef struct {
    GpuBufferHandle vertexBuffer;
    GpuBufferHandle indexBuffer;
    uint32_t triangleCount;
    uint32_t vertexStride;
    GpuFormat vertexFormat;
} GpuAccelerationStructureGeometry;

typedef struct {
    uint32_t geometryCount;
    const GpuAccelerationStructureGeometry* geometries;
    bool allowUpdate;
} GpuBottomLevelASDesc;

typedef struct {
    uint32_t instanceCount;
    GpuBufferHandle instanceBuffer;
} GpuTopLevelASDesc;

GpuResult gpuCreateBottomLevelAS(GpuDevice device, const GpuBottomLevelASDesc* desc, GpuHandle* outAS);
GpuResult gpuCreateTopLevelAS(GpuDevice device, const GpuTopLevelASDesc* desc, GpuHandle* outAS);
GpuResult gpuBuildAccelerationStructure(GpuCommandBuffer cmd, GpuHandle as, bool update);
GpuResult gpuDestroyAccelerationStructure(GpuDevice device, GpuHandle as);

GpuResult gpuCreateRayTracingPipeline(GpuDevice device, const GpuRayTracingPipelineDesc* desc, GpuPipelineHandle* outPipeline);
void gpuCmdTraceRays(GpuCommandBuffer cmd, GpuPipelineHandle rtPipeline, uint32_t width, uint32_t height, uint32_t depth);
```

### Example 测试

**文件**: `examples/18_raytracing/main.c`
**Shader**: `examples/18_raytracing/rt_scene.slang`

```slang
RaytracingAccelerationStructure gScene : register(t0);
RWTexture2D<float4> gOutput : register(u0);

[shader("raygeneration")]
void rayGenMain() {
    // 生成光线并追踪
    RayDesc ray;
    ray.Origin = ...;
    ray.Direction = ...;
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    float3 color = 0.0;
    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(gScene, RAY_FLAG_NONE, 0xFF, ray);
    while(q.Proceed()) {}
    if (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        color = float3(0.5, 0.7, 1.0);
    }
    gOutput[DispatchRaysIndex().xy] = float4(color, 1.0);
}
```

```c
int main() {
    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);
    if (!features.supported[GPU_FEATURE_RAY_TRACING]) {
        printf("Ray Tracing not supported, skipping\n");
        return 0;
    }

    // 构建 BLAS
    GpuBottomLevelASDesc blasDesc = { .geometryCount = 1, .geometries = &geom, .allowUpdate = false };
    GpuHandle blas;
    gpuCreateBottomLevelAS(device, &blasDesc, &blas);

    // 构建 TLAS
    GpuTopLevelASDesc tlasDesc = { .instanceCount = 1, .instanceBuffer = instanceBuf };
    GpuHandle tlas;
    gpuCreateTopLevelAS(device, &tlasDesc, &tlas);

    // 构建 AS
    GpuCommandBuffer buildCmd = gpuBeginCommandBuffer(device);
    gpuBuildAccelerationStructure(buildCmd, blas, false);
    gpuBuildAccelerationStructure(buildCmd, tlas, false);
    gpuEndCommandBuffer(buildCmd);
    gpuQueueSubmit(queue, 1, &buildCmd);
    gpuQueueWaitIdle(queue);

    // 光追渲染
    GpuPipelineHandle rtPipe;
    gpuCreateRayTracingPipeline(device, &rtDesc, &rtPipe);

    GpuCommandBuffer rtCmd = gpuBeginCommandBuffer(device);
    gpuCmdBindPipeline(rtCmd, rtPipe);
    gpuCmdTraceRays(rtCmd, rtPipe, width, height, 1);
    gpuEndCommandBuffer(rtCmd);

    gpuQueueSubmit(queue, 1, &rtCmd);
    gpuQueueWaitIdle(queue);

    printf("Ray Tracing rendered successfully\n");
    return 0;
}
```

### 验证流程

1. BLAS/TLAS 构建成功，无 validation 错误
2. 光追输出图像与参考图对比 (SSIM > 0.95)
3. Vulkan + D3D12 后端 RT 输出一致
4. Metal/WebGPU 后端返回 `GPU_ERROR_NOT_SUPPORTED`
5. 动态更新 AS (allowUpdate=true) 后重新追踪结果正确

---

## 后端专项适配测试矩阵

| 测试项 | Vulkan | D3D12 | Metal | WebGPU |
|:---|:---|:---|:---|:---|
| 设备创建 | MUST | MUST | SHOULD | SHOULD |
| Buffer CRUD | MUST | MUST | MUST | MUST |
| Texture CRUD | MUST | MUST | MUST | MUST |
| Compute Dispatch | MUST | MUST | MUST | MUST |
| Graphics Triangle | MUST | MUST | MUST | MUST |
| Bindless 索引 | MUST | MUST | SIMULATE | DEGRADE |
| Mesh Shader | MUST | MUST | N/A | N/A |
| Ray Tracing | MUST | MUST | N/A | N/A |
| Work Graph | N/A | MUST | N/A | N/A |
| Cooperative Matrix | MAY | MAY | N/A | N/A |
| Async Compute | MUST | MUST | SHOULD | SHOULD |
| Sparse Resource | MAY | MAY | N/A | N/A |

> MUST = 必须通过, SHOULD = 尽量通过, MAY = 可选, N/A = 不支持, SIMULATE = 模拟实现, DEGRADE = 降级实现

---

## Phase 3 集成测试

### 全功能神经渲染 Demo

**文件**: `examples/19_full_demo/main.c`

```c
int main() {
    // 1. 选择后端并初始化
    GpuBackendType backend = selectBackend(argc, argv);
    const GpuBackend_vtbl* vtbl;
    gpuBackendCreate(backend, &vtbl);
    vtbl->createDevice(&devDesc, &device);

    // 2. 构建 Feature Table
    GpuFeatureTable features;
    gpuBuildFeatureTable(device, &features);

    // 3. 构建 Ray Tracing 场景 (如果支持)
    GpuHandle tlas = GPU_NULL_HANDLE;
    if (features.supported[GPU_FEATURE_RAY_TRACING]) {
        buildRayTracingScene(device, &tlas);
    }

    // 4. 创建神经渲染网络
    GpuHandle neuralNet;
    GpuNeuralNetworkDesc nnDesc = { /* ... */ };
    gpuCreateNeuralNetwork(device, &nnDesc, &neuralNet);
    gpuLoadNeuralNetworkWeights(device, neuralNet, weights, weightSize);

    // 5. 配置 Work Graph (D3D12 专属)
    GpuWorkGraph wg = NULL;
    if (features.supported[GPU_FEATURE_WORK_GRAPH]) {
        createGpuDrivenPipeline(device, &wg);
    }

    // 6. 异步计算 + 图形渲染循环
    GpuQueueSet queues;
    gpuGetQueueSet(device, &queues);

    while (running) {
        // 异步计算: 神经推理
        GpuCommandBuffer aiCmd = gpuBeginCommandBuffer(device);
        gpuNeuralNetworkInference(aiCmd, neuralNet, inputTensor, outputTensor);
        gpuEndCommandBuffer(aiCmd);

        GpuFence aiFence;
        GpuQueueSemaphore aiDone = { queues.compute, aiFence, 1 };
        gpuQueueSubmitWithWait(queues.compute, 0, NULL, 1, &aiCmd, &aiDone);

        // 图形: 光追 + 传统渲染
        GpuCommandBuffer gfxCmd = gpuBeginCommandBuffer(device);
        if (tlas) {
            gpuCmdBindPipeline(gfxCmd, rtPipeline);
            gpuCmdTraceRays(gfxCmd, rtPipeline, w, h, 1);
        }
        gpuCmdBindPipeline(gfxCmd, gPipeline);
        gpuCmdBindShaderObject(gfxCmd, sceneBlock);
        gpuCmdDrawIndexed(gfxCmd, ...);
        gpuEndCommandBuffer(gfxCmd);

        GpuQueueSemaphore aiWait = { queues.compute, aiFence, 1 };
        gpuQueueSubmitWithWait(queues.graphics, 1, &aiWait, 1, &gfxCmd, NULL);

        present();
    }

    printf("Full neural rendering demo completed\n");
    return 0;
}
```

### Phase 3 里程碑验收

| 验收项 | 标准方法 |
|:---|:---|
| 神经渲染 Demo | T3.3 网络推理生成非平凡图像，输出维度正确 |
| GPU 驱动裁剪 | T3.4 Work Graph Cull→Draw 节点链执行正确 |
| 跨后端 RT | T3.5 Vulkan/D3D12 RT 输出视觉一致 |
| Feature Gating | 所有不支持特性优雅降级，无崩溃 |
| 后端矩阵覆盖 | 上表 MUST 项全部通过对应后端测试 |
| 性能基线 | Matmul 1024x1024 F16 达到硬件峰值 >40% |

---

## Example 项目汇总

| 编号 | 名称 | Phase | 覆盖任务 | 验证要点 |
|:---|:---|:---|:---|:---|
| 00a | window | 0 | T0.1 | SDL 窗口创建/resize |
| 00b | window_clear | 0 | T0.1-T0.3 | Swapchain 清屏呈现场景 |
| 01 | device_init | 1 | T1.1 | 设备创建/销毁 |
| 02 | shader_compile | 1 | T1.2 | Slang 编译多后端 |
| 03 | reflection | 1 | T1.3 | 类型反射 + JSON + TS 生成 |
| 04 | resource_lifecycle | 1 | T1.4 | 句柄生命周期 + Fence |
| 04b | barrier_demo | 1 | T1.4 | 资源屏障 compute→graphics |
| 05 | bindless_texture | 1 | T1.5 | Bindless 纹理采样 |
| 06 | triangle | 1 | T0-T1.6 | 完整渲染管线 + 屏幕呈现 |
| 07 | compute_pipeline | 2 | T2.1 | Compute Shader 前缀和 |
| 08 | material_system | 2 | T2.2 | ShaderObject + ParameterBlock |
| 09 | mesh_shader | 2 | T2.3 | Mesh Shader 三角形 |
| 10 | async_compute | 2 | T2.4 | 异步计算 + Fence 同步 |
| 11 | sparse_texture | 2 | T2.5 | 稀疏纹理映射/解映射 |
| 12 | pbr_material | 2 | T2.1-T2.5 | PBR 完整材质渲染 |
| 13 | backend_switch | 3 | T3.1 | 运行时后端切换 |
| 14 | feature_gating | 3 | T3.2 | Feature Table + Fallback |
| 15 | tensor_matmul | 3 | T3.3 | GPU 矩阵乘法 |
| 16 | neural_render | 3 | T3.3 | 神经网络推理 |
| 17 | workgraph | 3 | T3.4 | Work Graph 节点调度 |
| 18 | raytracing | 3 | T3.5 | 光线追踪渲染 |
| 19 | full_demo | 3 | T3.1-T3.5 | 全功能神经渲染集成 |
