
## 6. C API 层设计

### 6.1 API 设计原则

本 API 层旨在为上层引擎提供一个稳定、高性能且易于绑定的底层 GPU 抽象。

*   **Handle 化资源管理**：所有 GPU 资源（Buffer, Texture, Pipeline 等）均不直接暴露原始指针，而是通过不透明的句柄（Handle）进行操作。这为底层的资源重定向、延迟释放和流送（Streaming）提供了可能 [3]。
*   **PIMPL 模式与 ABI 稳定**：内部实现细节通过 PIMPL（Pointer to Implementation）模式隐藏在 C 接口之后，确保引擎在升级底层 RHI 库时无需重新编译 [3]。
*   **世代索引句柄系统**：句柄采用"索引 + 世代（Generation）"的复合结构，有效解决"悬空句柄"和"资源 ID 复用"导致的逻辑错误。
*   **C API 对外暴露**：采用纯 C 接口设计，极大方便了 Zig、Rust、TypeScript (via FFI) 等多种语言的绑定集成。

### 6.2 核心接口设计

#### 6.2.1 基础类型与状态码

```c
typedef int32_t GpuResult;
#define GPU_SUCCESS          0
#define GPU_ERROR_INVALID_ARGS -1
#define GPU_ERROR_OUT_OF_MEMORY -2
#define GPU_ERROR_NOT_SUPPORTED -3

typedef struct GpuDevice_t* GpuDevice;
typedef struct GpuCommandQueue_t* GpuCommandQueue;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;
```

#### 6.2.2 世代索引句柄系统

```c
typedef struct {
    uint32_t index;      // 资源在池中的索引
    uint32_t generation; // 世代号，用于验证句柄有效性
} GpuHandle;

typedef GpuHandle GpuBufferHandle;
typedef GpuHandle GpuTextureHandle;
typedef GpuHandle GpuShaderObjectHandle;
typedef GpuHandle GpuPipelineHandle;
```

#### 6.2.3 GpuDevice：设备创建与能力查询

```c
typedef struct {
    const char* appName;
    uint32_t adapterIndex;
    bool enableDebugLayer;
} GpuDeviceDesc;

// 创建设备
GpuResult gpuCreateDevice(const GpuDeviceDesc* desc, GpuDevice* outDevice);

// 能力查询系统 [1]
typedef struct {
    bool supportMeshShader;
    bool supportRayTracing;
    bool supportBindless;
    uint32_t maxTextureDimension;
} GpuCapabilities;

void gpuGetCapabilities(GpuDevice device, GpuCapabilities* outCaps);
```

#### 6.2.4 资源抽象与 Bindless 管理

```c
// GpuBindlessHeap：全局描述符堆管理 [3]
// 资源创建后自动获得一个全局唯一的索引
uint32_t gpuGetBindlessIndex(GpuDevice device, GpuHandle handle);

// 资源创建示例
typedef struct {
    size_t size;
    uint32_t usage; // GpuBufferUsageFlags
} GpuBufferDesc;

GpuResult gpuCreateBuffer(GpuDevice device, const GpuBufferDesc* desc, GpuBufferHandle* outHandle);
```

#### 6.2.5 命令模型与 ShaderObject

```c
// 基于 Slang-RHI 的 IShaderObject 机制 [6]
typedef struct {
    GpuPipelineHandle pipeline;
    const char* entryPoint;
} GpuShaderObjectDesc;

GpuResult gpuCreateShaderObject(GpuDevice device, const GpuShaderObjectDesc* desc, GpuShaderObjectHandle* outObj);

// 命令录制
void gpuCmdBindPipeline(GpuCommandBuffer cmd, GpuPipelineHandle pipeline);
void gpuCmdBindShaderObject(GpuCommandBuffer cmd, GpuShaderObjectHandle obj);
void gpuCmdDraw(GpuCommandBuffer cmd, uint32_t vertexCount, uint32_t instanceCount);
```

### 6.3 线程安全与多线程渲染模型

系统采用"线程局部命令缓冲"模型以支持高效的并行录制：

1.  **Device 级线程安全**：`GpuDevice` 的资源创建接口内部加锁，支持多线程并发调用。
2.  **CommandBuffer 独占性**：每个 `GpuCommandBuffer` 在录制期间由单个线程独占，无需加锁。
3.  **Queue 提交同步**：`GpuCommandQueue` 的提交操作是线程安全的，内部处理跨线程的提交顺序 [8]。

### 6.4 完整使用示例

以下展示了从初始化到命令提交的完整流程：

```c
// 1. 初始化设备
GpuDevice device;
GpuDeviceDesc desc = { .appName = "MyEngine", .enableDebugLayer = true };
gpuCreateDevice(&desc, &device);

// 2. 创建 Bindless 资源 [1]
GpuBufferDesc bufDesc = { .size = 1024 * 1024, .usage = GPU_BUFFER_USAGE_STORAGE };
GpuBufferHandle myBuffer;
gpuCreateBuffer(device, &bufDesc, &myBuffer);

// 获取 Bindless 索引，直接传入 Shader 使用
uint32_t bindlessIdx = gpuGetBindlessIndex(device, myBuffer);

// 3. 创建 ShaderObject (参数块) [6]
GpuShaderObjectHandle materialParams;
// ... 配置 desc 并创建 ...

// 4. 多线程录制命令
GpuCommandBuffer cmd = gpuBeginCommandBuffer(device); 
{
    gpuCmdBindPipeline(cmd, myGraphicsPipeline);
    gpuCmdBindShaderObject(cmd, materialParams); // 自动处理反射驱动的绑定 [3]
    gpuCmdDraw(cmd, 3, 1);
}
gpuEndCommandBuffer(cmd);

// 5. 提交到队列
GpuCommandQueue queue;
gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
gpuQueueSubmit(queue, 1, &cmd);
```

## 7. 技术风险与缓解措施

| 风险项 | 描述 | 缓解措施 |
| :--- | :--- | :--- |
| **Metal Bindless 缺失** | Metal 平台缺乏类似 Vulkan 的全局描述符索引支持 | 使用 Argument Buffers 模拟，或设计特定平台的资源索引回退方案 [6]。 |
| **多队列扩展困难** | Slang-RHI 目前仅支持 Graphics 队列，缺少异步计算 | Fork Slang-RHI 并重构 `ICommandQueue`，引入独立 Compute/Transfer 队列 [2]。 |
| **WebGPU 约束** | 暂不支持射线追踪和上传堆缓冲区 | 在 Web 端优先保证 AI Native 所需的 Tensor 计算接口，对高级图形特性进行功能降级 [11]。 |