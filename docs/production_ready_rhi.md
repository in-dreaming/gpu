# Production-ready RHI 路线图

本文档说明当前 `gpu` 仓库要成为游戏引擎可用的 production-ready RHI 还需要补齐的能力、推荐架构，以及可执行的实施路径。

当前工程已经具备一个有价值的 RHI 骨架：C API、opaque/generational handle、slang-rhi 后端封装、基础 buffer/texture/pipeline/queue/surface、bindless descriptor handle 获取、resource state 显式记录、Sponza data-driven demo 等。它可以支撑 demo 和 renderer prototype，但还不能直接作为正式游戏引擎 RHI。主要差距不是单个 feature 缺失，而是“跨后端行为合同、生命周期、同步、诊断、测试矩阵”还不够硬。

## 目标定义

Production-ready RHI 的目标不是暴露所有底层 API，而是给引擎上层提供稳定、可预测、可测试的 GPU 合同：

- 同一套 API 在 D3D12、Vulkan、Metal/WebGPU 等后端上语义一致。
- 错误不会静默成功； unsupported feature 必须可查询、可降级、可诊断。
- 资源生命周期不依赖全局 `wait idle`，能够支撑多帧 in-flight 和多线程提交。
- render graph 能自动处理 pass 依赖、barrier、transient resource、aliasing 和 presentation。
- shader reflection、pipeline layout、descriptor layout、bindless、push/root constants 有稳定 ABI。
- 所有核心路径有 conformance tests，能在 CI 中跨后端验证。
- debug/profile/capture 能力足够支持实际项目排障。

## 推荐总体架构

建议将 RHI 分为五层，避免所有能力堆在单个 `gpu_device.cpp` 或 feature 文件里：

```text
Application / Engine Renderer
  |
Render Graph / Frame Scheduler
  |
RHI Frontend: C API, handles, validation, feature contract
  |
RHI Core: resources, state tracker, descriptors, pipeline cache, upload/lifetime
  |
Backend Adapter: slang-rhi + backend-specific capability/fallback glue
```

各层职责：

- `RHI Frontend`：只处理 public API、参数校验、错误码、handle generation、debug label。
- `RHI Core`：维护真实状态机和引擎语义，包括 barrier、descriptor allocator、deferred destroy、upload ring、pipeline layout cache。
- `Render Graph`：把高层 pass/resource intent 转换为命令录制和 barrier，不让业务代码手写大量 resource transition。
- `Backend Adapter`：只封装 slang-rhi 和必要的 backend workaround，不把 D3D12/Vulkan 细节泄漏到 public API。
- `Conformance`：用同一批测试验证所有后端的行为一致性。

## 1. Backend Contract 与能力表

### 当前问题

当前能力查询已经存在，但还不够作为 engine contract。部分模块过去会在底层不支持时返回假成功，现在 sparse 已经改为明确 `GPU_ERROR_NOT_SUPPORTED`，但其他高级能力仍需要统一规则。

### 要做什么

- 建立 `GpuBackendCaps`，区分 `supported`、`emulated`、`unsupported` 三种状态。
- 每个高级特性都必须有 fallback 策略：
  - `native`：后端原生支持。
  - `emulated`：RHI 层可正确模拟，但必须声明性能代价。
  - `unsupported`：返回 `GPU_ERROR_NOT_SUPPORTED`，不得假成功。
- 对 bindless、ray tracing、mesh shader、work graph、sparse resource、timeline fence、descriptor indexing、multi-queue、present mode 做细粒度 caps。
- 所有 public create/request 函数在进入后端前先过 feature gate。

### 推荐接口

```c
typedef enum GpuFeatureSupport {
    GPU_FEATURE_SUPPORT_UNSUPPORTED = 0,
    GPU_FEATURE_SUPPORT_EMULATED = 1,
    GPU_FEATURE_SUPPORT_NATIVE = 2,
} GpuFeatureSupport;

typedef struct GpuFeatureInfo {
    GpuFeatureSupport support;
    uint32_t limit;
    const char* reason;
} GpuFeatureInfo;

GpuResult gpuGetFeatureInfo(GpuDevice device, GpuFeature feature, GpuFeatureInfo* outInfo);
```

### 验收标准

- 每个 feature 都有测试验证 unsupported path。
- 示例程序不能依赖未声明能力。
- CI 输出后端能力 JSON，便于比对机器差异。

## 2. Resource Lifetime 与内存系统

### 当前问题

当前 destroy 路径为了正确性可以等待 queue idle，但游戏引擎不能频繁全局等待。需要多帧延迟销毁和资源回收机制。

### 要做什么

- 建立 frame index / timeline fence 驱动的 deferred destroy queue。
- 资源销毁只把对象放入 retire list，等 GPU fence 到达后释放。
- 区分 committed resource、placed/aliasing resource、upload/readback/transient resource。
- 建立 upload ring buffer：
  - per-frame staging pages。
  - 大上传走 dedicated staging allocation。
  - 支持 buffer/texture row pitch 和 subresource layout。
- 建立 readback queue，避免同步 map 阻塞主线程。

### 推荐结构

```cpp
struct GpuRetiredObject {
    enum Kind { Buffer, Texture, TextureView, Pipeline, ShaderObject } kind;
    uint64_t retireFenceValue;
    void* object;
};

struct GpuFrameContext {
    uint64_t frameIndex;
    uint64_t submittedFenceValue;
    UploadArena uploadArena;
    std::vector<GpuRetiredObject> retiredObjects;
};
```

### 验收标准

- destroy 不调用全局 `waitOnHost`。
- N 帧 in-flight 下资源销毁、重建、slot reuse 不产生 use-after-free。
- 有压力测试：每帧创建/销毁大量 buffer/texture，跑 10000 帧无泄漏、无 validation error。

## 3. Resource State Tracker 与 Barrier Resolver

### 当前问题

当前有显式状态记录，但还不是完整 hazard resolver。引擎上层仍容易忘记 transition，或者在多 pass、多 queue 下产生错误。

### 要做什么

- 为每个 resource/subresource 维护状态：
  - whole resource state。
  - texture mip/layer/aspect state。
  - queue ownership。
- 支持 read state 合并、write state 冲突检测。
- Render Graph 编译阶段根据 pass read/write intent 自动插入 barrier。
- 手写 command path 仍允许显式 transition，但 debug 模式要验证状态。
- 支持 aliasing barrier、UAV barrier/global barrier、present acquire/release。

### 推荐模型

```cpp
struct GpuSubresourceState {
    GpuResourceState state;
    GpuQueueType ownerQueue;
    uint64_t lastWriterPass;
};

struct GpuResourceStateRecord {
    GpuHandle handle;
    bool splitPerSubresource;
    GpuSubresourceState whole;
    std::vector<GpuSubresourceState> subresources;
};
```

### 验收标准

- render graph 测试能自动完成 `CopyDest -> ShaderResource -> RenderTarget -> Present`。
- debug 模式下错误读写顺序能报出资源名、pass 名、旧状态、新状态。
- 多 queue transfer->graphics ownership transfer 有测试。

## 4. Render Graph / Frame Graph

### 当前问题

当前 render pass 和 command API 仍偏底层，缺少正式 frame graph。游戏引擎需要让 pass 描述“读什么、写什么”，而不是每处手写 barrier 和 lifetime。

### 要做什么

- 增加 graph builder：
  - logical resources。
  - imported resources，如 swapchain backbuffer。
  - pass read/write declarations。
  - attachment load/store/clear/resolve。
- graph compile 阶段：
  - cull unused pass。
  - 计算 pass 顺序。
  - 分配 transient resource。
  - 插入 barriers。
  - alias compatible transient resources。
- graph execute 阶段：
  - 创建 command encoder。
  - begin/end render/compute/copy pass。
  - 调用 pass callback。

### 推荐 API 草案

```c
GpuGraph graph;
gpuGraphCreate(device, &graph);

GpuGraphTexture gbuffer =
    gpuGraphCreateTexture(graph, &desc, "gbuffer.albedo");

gpuGraphAddRenderPass(graph, "gbuffer")
    .writeColor(gbuffer, GPU_LOAD_CLEAR, GPU_STORE_STORE)
    .writeDepth(depth, GPU_LOAD_CLEAR, GPU_STORE_STORE)
    .execute(callback, userData);

gpuGraphCompile(graph);
gpuGraphExecute(graph, queue);
```

### 验收标准

- 示例 `deferred + shadow + postprocess + present` 不需要用户手写 texture state transition。
- transient attachment 可以复用内存。
- graph dump 可输出 DOT/JSON，便于调试。

## 5. Descriptor / Bindless / Pipeline Layout

### 当前问题

bindless 现在已经能获取真实 slang-rhi descriptor handle，但还需要完整 layout contract：shader reflection 到 descriptor layout、pipeline layout、parameter block、bindless table 的关系必须稳定。

### 要做什么

- 从 Slang reflection 生成统一 `GpuPipelineLayout`：
  - descriptor set/space。
  - binding。
  - resource kind。
  - array count。
  - bindless handle field。
  - push/root constants。
- 建立 descriptor allocator：
  - transient descriptors。
  - persistent descriptors。
  - bindless descriptor heap/table。
  - sampler heap。
- `GpuBindlessHeap` 支持 texture、buffer、sampler、acceleration structure。
- 支持 descriptor update lifetime：资源被释放时 bindless slot 要 invalid 或 retain。
- pipeline cache key 必须包含 shader identity、entry point、layout hash、render target format、raster/depth/blend state。

### 推荐结构

```cpp
struct GpuBindingLayout {
    uint32_t set;
    uint32_t binding;
    GpuResourceKind kind;
    uint32_t count;
    bool bindless;
};

struct GpuPipelineLayout {
    uint64_t layoutHash;
    std::vector<GpuBindingLayout> bindings;
    std::vector<GpuPushConstantRange> pushConstants;
};
```

### 验收标准

- reflection 结果稳定序列化为 JSON。
- 同一 shader 在 D3D12/Vulkan 下 binding layout 一致或有明确映射。
- bindless texture/buffer/sampler 都有 conformance tests。
- pipeline cache 命中率可统计。

## 6. Command Model 与多线程录制

### 当前问题

当前 command model 已经避免了空 render pass 假成功，但还需要明确 command allocator、command list、pass encoder 的 ownership/lifetime。

### 要做什么

- 建立 per-thread command context pool。
- 明确 command buffer 状态机：
  - initial。
  - recording。
  - executable。
  - submitted。
  - retired。
- 支持 bundle/secondary command buffer 或 backend 等价模拟。
- 所有 `gpuCmd*` 在 debug 模式下校验当前 pass 类型。
- graphics/compute/copy 命令分离，避免 compute pass 自动创建等隐式行为长期存在。

### 验收标准

- 多线程录制 100 个 pass，主线程提交，不崩溃、不数据竞争。
- 错误 pass 类型调用返回可诊断 validation message。
- command buffer 重复提交、提交后录制、未 finish 提交都有测试。

## 7. Swapchain / Surface / Presentation

### 当前问题

示例和基础 surface 已可运行，但引擎需要完整 present contract。

### 要做什么

- 统一 acquire/present 状态转换。
- 支持 resize、format change、surface lost、minimized window。
- 支持 present mode 查询与选择：vsync、mailbox、immediate。
- 支持 HDR、color space、sRGB policy。
- backbuffer 作为 imported graph texture 接入 render graph。

### 验收标准

- resize storm 测试通过。
- alt-tab/minimize/recreate 测试通过。
- present 前状态必须为 `PRESENT`，debug 模式验证。

## 8. Shader Toolchain 与 Pipeline Cache

### 当前问题

已有 Slang 编译基础，但 production-ready 需要工程化 shader DB、cache、热重载和稳定 diagnostics。

### 要做什么

- Shader asset pipeline：
  - source path。
  - module name。
  - entry points。
  - defines/capabilities。
  - target profile。
  - reflection JSON。
  - binary cache。
- 支持 offline compile 和 runtime fallback compile。
- pipeline cache 分两层：
  - shader binary cache。
  - PSO cache。
- diagnostic 统一输出文件、行号、entry point、target、backend。
- shader hot reload 必须安全替换 pipeline，旧 pipeline 延迟销毁。

### 验收标准

- 改 shader 后 demo 可热重载。
- cache cold/warm 时间可统计。
- shader compile 失败能定位到源码行。

## 9. Debug / Validation / Profiling

### 当前问题

缺少引擎开发所需的“出了错知道为什么”的系统。

### 要做什么

- Debug name 覆盖所有 backend object。
- Validation callback：
  - severity。
  - object label。
  - command/pass context。
  - suggested fix。
- GPU markers：
  - begin/end event。
  - pass marker。
  - resource marker。
- Timestamp query / pipeline statistics。
- GPU crash breadcrumbs：
  - D3D12 DRED/Aftermath 可选。
  - Vulkan device lost diagnostics。

### 验收标准

- RenderDoc/PIX/Nsight capture 里对象和 pass 有可读名称。
- validation test 能精确报告错误 API 和资源名。
- frame profiler 可输出每个 pass GPU time。

## 10. Testing 与 CI

### 当前问题

已有 smoke/phase tests，但 production-ready 需要 backend conformance 和长期压力测试。

### 要做什么

- 测试分层：
  - unit：handle pool、descriptor allocator、state tracker。
  - conformance：同一 API 在所有后端行为一致。
  - render：小图 readback 比对。
  - stress：资源 churn、多线程、resize、device lost。
  - examples：所有 examples 至少 headless smoke。
- 每个 feature 有 native/unsupported path 测试。
- CI matrix：
  - Windows D3D12。
  - Windows Vulkan。
  - Linux Vulkan。
  - macOS Metal，如果后端接入。
  - CPU/null backend，用于快速合同测试。

### 验收标准

- 每个 PR 跑 unit + smoke。
- nightly 跑 full conformance + stress。
- 渲染测试输出图片 diff。

## 11. Virtual Texture / Streaming / Sparse

### 当前问题

Sponza demo 可以展示数据驱动和软件 VT 思路，但 production VT 需要完整 residency、page table、feedback 和 streaming pipeline。slang-rhi 当前 sparse 能力未统一暴露时，RHI 应明确 unsupported。

### 要做什么

- 软件 VT 路线：
  - physical texture atlas。
  - page table texture。
  - feedback buffer。
  - CPU/GPU page request resolve。
  - streaming upload queue。
- 硬件 sparse 路线：
  - 后端暴露 sparse caps 后接入 native sparse mapping。
  - 和软件 VT 共享上层 residency manager。
- streaming manager：
  - 支持 row pitch、slice pitch、多个 subresource。
  - 支持 upload budget、优先级、取消请求。
  - 支持异步 transfer queue。

### 验收标准

- 大场景 texture working set 可控。
- camera 快速移动时不会阻塞主线程。
- page miss、resident pages、upload bandwidth 有统计。

## 12. 分阶段实施计划

### Phase A：合同硬化

- 完成 feature support 三态能力表。
- 所有 unsupported feature 返回明确错误。
- resource/command/pipeline public API 增加 validation。
- 建立 validation callback。

### Phase B：生命周期与同步

- 引入 frame context、timeline fence、deferred destroy。
- 替换 destroy path 中的 `wait idle`。
- 建立 upload ring/readback queue。
- 完成 subresource state tracker。

### Phase C：Render Graph

- 增加 graph resource/pass builder。
- 实现 graph compile barrier。
- swapchain backbuffer 作为 imported resource。
- 迁移至少两个 example 到 graph path。

### Phase D：Descriptor/Layout/Pipeline

- Slang reflection 生成 `GpuPipelineLayout`。
- descriptor allocator 和 bindless table 完整化。
- pipeline cache key 和 serialization。
- shader reflection JSON/conformance tests。

### Phase E：Backend conformance

- 后端矩阵测试。
- render readback tests。
- stress tests。
- capture marker/profiling 完整化。

### Phase F：Engine integration

- 接入真实 renderer workload。
- 接入 asset/shader pipeline。
- 接入 hot reload。
- 性能基准和内存基准。

## 结论

当前工程适合作为游戏引擎 RHI 的基础骨架继续推进，但 production-ready 的关键不在“再加几个 demo”，而在把语义合同做硬：能力表、生命周期、barrier、render graph、descriptor layout、pipeline cache、debug/profiling、conformance tests。推荐优先实现 Phase A 到 Phase C；完成后，这个 RHI 才会从“可演示”进入“可承载真实 renderer”的阶段。
