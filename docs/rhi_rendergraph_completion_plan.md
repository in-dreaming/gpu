# RHI 与 RenderGraph 完善改造说明

本文档基于当前代码审查结果，说明 `RHI` 与 `RenderGraph` 已具备的 baseline、主要缺口，以及建议的改造路径。目标不是一次性重写，而是把现有可运行骨架逐步补齐为可作为引擎核心层使用的稳定实现。

## 当前状态

| 模块 | 当前状态 | 主要依据 | 完成度判断 |
| :--- | :--- | :--- | :--- |
| RHI texture | 已有创建、销毁、view、subresource view | `src/gpu/core/gpu_texture.h` | baseline 完成 |
| RHI buffer | 已有创建、初始化、上传、下载、销毁 | `src/gpu/core/gpu_buffer.h` | baseline 完成 |
| RHI pipeline | 已有 graphics / compute pipeline | `src/gpu/core/gpu_pipeline.h` | baseline 完成 |
| descriptor | 有 descriptor pool 统计与 bindless descriptor handle | `src/gpu/bindless/*` | 普通 descriptor set 不完整 |
| bindless baseline | 有 texture / buffer / sampler slot 分配和 lookup | `src/gpu/bindless/gpu_bindless_heap.cpp` | baseline 完成 |
| graphics / compute queue | API 有 graphics / compute / transfer；Vulkan/D3D12/Metal 为独立队列 | `gpuGetQueueInfo`, `gpu_device.cpp` | baseline 完成 |
| barrier | 有 resource state API 和 graph 内状态推导 | `src/gpu/resource/gpu_barrier.*` | baseline 完成，hazard tracking 不完整 |
| timestamp / marker | 有 timestamp query pool、frequency、debug marker | `src/gpu/queue/gpu_multi_queue.*`, `src/gpu/debug/*` | baseline 完成 |
| RenderGraph pass / resource | 有 logical resource、import、pass、callback | `src/gpu/rendergraph/gpu_render_graph.*` | baseline 完成 |
| transient texture | compile 时创建非 imported resource，destroy/reset 释放；Vulkan 支持 placed heap 原型 | `gpuGraphCompile`, `gpu_transient_heap.*` | 池化完成；通用 heap aliasing 未完成 |
| barrier tracking | 按 pass access 生成 state transition | `gpuGraphCompile` | 缺 subresource / queue ownership / diagnostics |
| debug graph | DOT / JSON export | `gpuGraphExportDot`, `gpuGraphExportJson` | baseline 完成 |

## 改造目标

1. RHI public API 要表达清楚“支持什么、不支持什么、如何降级”。
2. descriptor / pipeline layout / bindless 要形成稳定合同，而不是只暴露底层 handle。
3. queue 要区分“API 类型”和“真实硬件队列”，不能把同一队列伪装成异步能力。
4. barrier 要从手动 set-state 升级为 hazard resolver，可诊断错误读写顺序。
5. RenderGraph 要管理 pass dependency、resource lifetime、transient allocation、barrier、debug dump。
6. 测试要覆盖失败路径、跨 pass hazard、跨 queue sync、资源生命周期和 debug dump 内容。

## 1. Queue 能力改造

### 当前问题（已部分解决）

独立队列已在 Vulkan / D3D12 / Metal 落地；WebGPU / D3D11 / CPU / CUDA 仍为 alias。剩余工作：更完整的 queue ownership barrier 诊断与 CUDA 多 stream。

### 建议改造

- 增加 queue capability 查询：

```c
typedef enum GpuQueueSupport {
    GPU_QUEUE_SUPPORT_UNAVAILABLE = 0,
    GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS = 1,
    GPU_QUEUE_SUPPORT_DEDICATED = 2,
} GpuQueueSupport;

typedef struct GpuQueueInfo {
    GpuQueueType type;
    GpuQueueSupport support;
    uint32_t familyIndex;
    const char* reason;
} GpuQueueInfo;

GpuResult gpuGetQueueInfo(GpuDevice device, GpuQueueType type, GpuQueueInfo* outInfo);
```

- 当 backend 只有 graphics queue 时，`gpuGetQueue(device, GPU_QUEUE_TYPE_COMPUTE, ...)` 可以继续返回 alias queue，但 `gpuGetQueueInfo` 必须报告 `GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS`。
- 如果后续 fork / 扩展 slang-rhi 支持独立 queue，则 compute / transfer 应保存真实 `ICommandQueue`。
- RenderGraph compile 时根据 queue support 决定是否允许 async compute pass；alias queue 下只能串行执行。

### 验收标准

- 测试能区分 dedicated queue 与 alias queue。
- async compute 示例在 alias queue 下不会宣称并行收益。
- 跨 queue submit 必须有 fence wait / signal 测试。

## 2. Descriptor 与 Pipeline Layout 完善

### 当前问题

Bindless 已能分配并保存真实 descriptor handle，但普通 descriptor set 只有 pool 和统计信息，没有完整 allocate / update / bind 语义。`gpuCreateBindlessHeap2` 对 mixed heap 的处理也只是选择一个主类型，不能算真正 mixed heap。

### 建议改造

- 明确拆分三类描述符路径：
  - `GpuDescriptorSet`：传统 set / binding 模式。
  - `GpuBindlessHeap`：全局数组索引模式。
  - `GpuParameterBlock`：由 Slang reflection 驱动的高层绑定模式。
- 补齐 descriptor set API：

```c
typedef struct GpuDescriptorSet_t* GpuDescriptorSet;

GpuResult gpuAllocateDescriptorSet(
    GpuDescriptorPool pool,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet* outSet);

GpuResult gpuUpdateDescriptorSet(
    GpuDescriptorSet set,
    uint32_t binding,
    uint32_t arrayIndex,
    const GpuDescriptorWrite* writes,
    uint32_t writeCount);

void gpuCmdBindDescriptorSet(
    GpuRenderPassEncoder pass,
    GpuPipelineLayout layout,
    uint32_t setIndex,
    GpuDescriptorSet set);
```

- mixed bindless heap 必须真实记录每个 slot 的 descriptor type，不能用 heap 级单一 `descriptorType` 代替。
- `GpuPipelineLayout` 应由 Slang reflection 生成，并缓存 layout hash。
- pipeline 创建时校验 shader reflection layout 与 descriptor / bindless 配置是否匹配。

### 验收标准

- 一个 shader 同时使用 sampled texture、storage buffer、sampler、constant buffer 时可以通过 descriptor set 正确绑定。
- bindless mixed heap 能在同一 heap 中注册 texture / buffer / sampler，并能按 type 验证非法访问。
- descriptor update 越界、类型不匹配、未绑定资源在 debug 模式下返回明确错误。

## 3. Barrier 与 Hazard Resolver

### 当前问题

当前 `gpuCmdSetBufferState` / `gpuCmdSetTextureState` 会调用底层 `set*State` 并记录 whole-resource state。RenderGraph compile 也会根据 pass access 生成 transition。但这还不是完整 hazard resolver：缺 subresource、queue ownership、aliasing barrier、UAV/global barrier 规则和诊断信息。

### 建议改造

- 引入统一状态记录：

```cpp
struct GpuSubresourceState {
    GpuResourceState state;
    GpuQueueType ownerQueue;
    uint32_t lastWriterPass;
    uint32_t lastReaderPass;
};

struct GpuResourceStateRecord {
    GpuHandle handle;
    bool splitPerSubresource;
    GpuSubresourceState whole;
    std::vector<GpuSubresourceState> subresources;
};
```

- barrier resolver 输入不应只是目标状态，还应包含 access intent：

```c
typedef enum GpuAccessFlags {
    GPU_ACCESS_SHADER_READ       = 1u << 0,
    GPU_ACCESS_SHADER_WRITE      = 1u << 1,
    GPU_ACCESS_COLOR_ATTACHMENT  = 1u << 2,
    GPU_ACCESS_DEPTH_READ        = 1u << 3,
    GPU_ACCESS_DEPTH_WRITE       = 1u << 4,
    GPU_ACCESS_COPY_READ         = 1u << 5,
    GPU_ACCESS_COPY_WRITE        = 1u << 6,
    GPU_ACCESS_PRESENT           = 1u << 7,
} GpuAccessFlags;
```

- RenderGraph compile 输出显式 barrier plan，包含：
  - resource name。
  - before / after state。
  - source pass / destination pass。
  - subresource range。
  - queue ownership transfer。
- debug 模式下验证手写 command path：
  - 未 transition 就读写时报错。
  - write-after-write、read-after-write、write-after-read hazard 给出 pass 名和资源名。

### 验收标准

- 有测试覆盖 `CopyDest -> ShaderResource -> RenderTarget -> Present`。
- depth texture 能正确从 `DepthWrite` 转到 `DepthRead` / `ShaderResource`。
- UAV write 后的 read 或 write 能插入 UAV/global barrier。
- DOT/JSON dump 可以看到 barrier 列表。

## 4. RenderGraph Transient Resource 与 Aliasing

### 当前问题

当前 graph 中非 imported texture / buffer 会在 compile 时创建，在 graph destroy/reset 时释放。这是 transient resource 的第一步，但还没有 allocator、lifetime analysis、aliasing 和内存复用。

### 建议改造

- compile 阶段计算每个 resource lifetime：
  - first use pass。
  - last use pass。
  - usage flags。
  - size / format / sample count。
- 增加 transient allocator：

```cpp
struct GpuTransientResourceDesc {
    GpuGraphResourceKind kind;
    uint64_t size;
    GpuFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t sampleCount;
    GpuTextureUsage textureUsage;
    GpuBufferUsage bufferUsage;
};

struct GpuTransientAllocation {
    uint32_t heapIndex;
    uint64_t offset;
    uint64_t size;
    bool aliasable;
};
```

- 第一阶段可以只做 resource pooling：同规格资源复用对象，不做 placed resource aliasing。
- 第二阶段再做真实 aliasing：lifetime 不重叠且格式 / usage 兼容的资源共用 heap allocation。
- imported resource、swapchain texture、readback resource 默认不可 alias。

### 验收标准

- 1000 帧 graph 编译执行不会重复无界增长资源。
- 两个 lifetime 不重叠的同规格 transient texture 能复用 allocation。
- graph JSON 输出 transient lifetime 与 allocation id。

## 5. RenderGraph Execute 模型

### 当前问题

当前 `gpuGraphExecute` 是每个 pass 创建一个 encoder 并单独 submit。实现简单，但 submit 粒度过细，无法在单个 command buffer 内表达完整 frame，也不利于后续 async queue scheduling。

### 建议改造

- 增加 execute mode：

```c
typedef enum GpuGraphExecuteMode {
    GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER = 0,
    GPU_GRAPH_EXECUTE_PASS_SUBMIT = 1,
    GPU_GRAPH_EXECUTE_MULTI_QUEUE = 2,
} GpuGraphExecuteMode;
```

- 默认使用 single command buffer：
  - graph execute 开始创建一个 encoder。
  - 按 execution order 依次 emit barrier 和 pass。
  - finish 后一次 submit。
- pass submit 仅作为 debug / bisect 模式保留。
- multi queue 模式根据 pass queue intent 分组，并插入 fence wait / signal。

### 验收标准

- 单 command buffer 模式通过现有 Phase C 测试。
- debug 模式可切换到 pass submit，用于定位具体 pass crash。
- 多 queue 模式在 alias queue 下自动降级为单队列串行。

## 6. Timestamp 与 Marker API 调整

### 当前问题

timestamp 已有 query pool 和 `gpuCmdWriteTimestamp`，但函数接收 `GpuCommandBuffer`，而一般 timestamp 更自然地写在 encoder 或 pass encoder 上。当前实现也要求调用者理解 command buffer 何时 finalize，容易误用。

### 建议改造

- 保留旧 API 兼容，但新增更直接的接口：

```c
void gpuCmdWriteTimestampEncoder(
    GpuCommandEncoder encoder,
    GpuQueryPool pool,
    uint32_t queryIndex);

void gpuCmdWriteRenderTimestamp(
    GpuRenderPassEncoder pass,
    GpuQueryPool pool,
    uint32_t queryIndex);

void gpuCmdWriteComputeTimestamp(
    GpuComputePassEncoder pass,
    GpuQueryPool pool,
    uint32_t queryIndex);
```

- RenderGraph 增加可选 pass profiling：
  - compile 后为每个 pass 分配 begin/end query。
  - execute 时自动写 timestamp。
  - frame end 后读取 GPU duration。
- marker 自动包裹 graph pass：
  - pass 开始 push debug group。
  - pass 结束 pop debug group。

### 验收标准

- 一个 graph pass 能输出 GPU duration。
- marker capture 中能看到 graph pass 名。
- timestamp API 在 encoder / render pass / compute pass 三种上下文都有测试。

## 7. Debug Graph 输出增强

### 当前问题

当前 DOT / JSON 只输出 pass、resource、dependency、access，适合确认结构，但不足以定位 barrier、lifetime、culled pass 和 transient allocation 问题。

### 建议改造

- JSON 增加字段：
  - pass execution order。
  - pass queue type。
  - pass culled reason。
  - resource first / last use。
  - resource imported / transient / aliased。
  - barrier list。
  - validation warnings。
- DOT 增加颜色规则：
  - imported resource。
  - transient resource。
  - culled pass。
  - async compute pass。
  - present pass。
- 增加字符串导出接口，方便测试不落盘：

```c
GpuResult gpuGraphExportJsonString(GpuGraph graph, char* outBuffer, size_t* inOutSize);
GpuResult gpuGraphExportDotString(GpuGraph graph, char* outBuffer, size_t* inOutSize);
```

### 验收标准

- 测试能解析 JSON 并验证 barrier 数量、pass 顺序、culled 状态。
- DOT 输出包含 resource 节点，不只是 pass 节点。
- 导出函数对非法路径、buffer 不足有明确错误码。

## 8. 推荐实施顺序

### P0：先修正语义和易踩坑点

- 增加 `gpuGetQueueInfo`，明确 compute / transfer 当前是 graphics alias。
- 修正 `gpuQueueSubmitWithSync`，提交前应与 `gpuQueueSubmit` 一样 finalize command buffer。
- 为 timestamp 增加 encoder / pass 级 API。
- graph execute 增加 single command buffer 模式。

### P1：补齐 descriptor 和 graph debug

- 完成普通 descriptor set allocate / update / bind。
- mixed bindless heap 改为 per-slot type。
- graph JSON / DOT 输出 resource 节点、barrier、lifetime。
- debug marker 自动包裹 graph pass。

### P2：强化 barrier resolver

- 将 RenderGraph barrier plan 独立为可测试结构。
- 支持 subresource range。
- 支持 UAV/global barrier。
- 支持 imported resource final state。
- 增加 hazard diagnostics。

### P3：完善 transient allocator

- 先做同规格 resource pool。
- 再做 lifetime-based aliasing。
- 最后接入 backend placed resource / heap allocation。

### P4：真正 multi-queue

- 评估是否 fork / 扩展 slang-rhi queue model。
- 引入 queue family / ownership transfer。
- RenderGraph scheduler 支持 graphics / compute / transfer 分组。
- 增加 async compute demo 和性能验证。

## 测试矩阵

| 测试 | 覆盖内容 |
| :--- | :--- |
| `phaseC_graph_baseline` | pass/resource/import/export/execute |
| `graph_barrier_states` | read/write/copy/render/present state transition |
| `graph_subresource_barrier` | mip/layer transition |
| `graph_transient_pool` | resource lifetime 与复用 |
| `graph_debug_dump` | DOT/JSON 内容可解析 |
| `descriptor_set_bind` | ordinary descriptor set update/bind |
| `bindless_mixed_heap` | mixed type slot 和非法类型校验 |
| `queue_alias_info` | compute/transfer alias graphics 的能力报告 |
| `queue_sync_finalize` | `gpuQueueSubmitWithSync` 正确 finalize command buffer |
| `timestamp_pass_profile` | encoder/pass timestamp 和 graph profiling |

## 完成定义

只有满足以下条件，才建议把清单标记为“完整实现”：

- API 层能区分 native / emulated / unsupported。
- descriptor set 与 bindless 都有稳定绑定合同。
- queue capability 不误报异步能力。
- RenderGraph 默认不需要用户手写 texture/buffer state transition。
- graph dump 能解释 pass 顺序、resource lifetime、barrier 和 culled 原因。
- transient resource 至少有 pooling，最好有 aliasing。
- 所有关键路径都有自动化测试，并覆盖错误路径。

