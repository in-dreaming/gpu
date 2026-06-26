# GPU 层特性说明

本文档描述当前 `gpu` 库已落地、可验证的公共能力，面向引擎集成与示例开发。更细的改造计划见 [rhi_rendergraph_completion_plan.md](rhi_rendergraph_completion_plan.md)。

## 1. 设计原则

| 原则 | 说明 |
|------|------|
| 纯 C API | 公共头文件为 C11，`extern "C"` 包裹；C++ 实现隐藏在 PIMPL 中 |
| Shader-Centric | 以 Slang 反射驱动资源布局，减少手写 binding 表 |
| 不透明句柄 | `GpuDevice`、`GpuGraph` 等为指针类型；资源使用 index + generation 代际句柄 |
| 能力显式报告 | 队列、heap、bindless 等能力通过查询 API 报告，alias 后端不会伪装成独立异步队列 |
| 后端可移植 | 基于 [slang-rhi](https://github.com/shader-slang/slang-rhi)，Windows / Linux / macOS 可选多后端 |

入口头文件：

```c
#include "gpu/gpu.h"
```

---

## 2. RHI 核心

### 2.1 设备与后端

- `gpuCreateDevice`：按 `GpuDeviceDesc.preferredBackend` 创建，默认自动选择（Default → Vulkan → D3D12）
- `gpuGetBackendType` / `gpuGetBackendName`：查询实际后端
- 支持后端：Vulkan、D3D12、D3D11、Metal、WebGPU、CPU、CUDA（按 CMake 选项与平台启用）

### 2.2 资源

| 类型 | 主要 API | 能力 |
|------|----------|------|
| Buffer | `gpuCreateBuffer`, `gpuDestroyBuffer` | 上传、下载、map/unmap、多种 usage 标志 |
| Texture | `gpuCreateTexture`, `gpuCreateTextureView` | 2D/3D/Cube、MIP、array layer、子资源 view |
| Sampler | `gpuCreateSampler` | 过滤与寻址模式 |

### 2.3 命令录制

- `gpuBeginCommandEncoder` → render / compute pass → `gpuFinishCommandEncoder`
- `gpuQueueSubmit` / `gpuQueueWaitOnHost`
- `gpuCmdCopyBuffer`：buffer 间 GPU copy（自动处理 CopySource/CopyDest 状态）
- Debug marker、timestamp query pool（`gpuCreateQueryPool` 等）

### 2.4 平台与交换链

- SDL3 窗口与事件：`gpuPlatformInit`, `gpuCreateWindow`, `gpuPollEvent`
- Surface：`gpuCreateSurface`, `gpuSurfaceAcquireNextImage`, `gpuSurfacePresent`
- 统一 swapchain 封装：`gpu_swapchain.h`

---

## 3. 多队列与同步

### 3.1 队列类型

```c
typedef enum {
    GPU_QUEUE_TYPE_GRAPHICS = 0,
    GPU_QUEUE_TYPE_COMPUTE  = 1,
    GPU_QUEUE_TYPE_TRANSFER = 2,
} GpuQueueType;
```

### 3.2 能力查询

```c
GpuQueueInfo info;
gpuGetQueueInfo(device, GPU_QUEUE_TYPE_COMPUTE, &info);
// info.support: DEDICATED | ALIAS_GRAPHICS | UNAVAILABLE
// info.reason: 人类可读说明（含后端限制原因）
```

`gpuDeviceSupportsIndependentQueues(device)` 在 compute 与 transfer 均为 **DEDICATED** 时返回 true。

### 3.3 后端多队列矩阵

| 后端 | Graphics | Compute | Transfer | 说明 |
|------|----------|---------|----------|------|
| Vulkan | 独立 | 独立 | 独立 | 按 queue family 分配 |
| D3D12 | Direct | Compute | Copy | 三个 D3D12 command queue |
| Metal | 独立 | 独立 | 独立 | 三个 `MTLCommandQueue`，`MTLSharedEvent` 跨队列同步 |
| WebGPU | Alias | Alias | Alias | 规范仅单队列 |
| D3D11 | Alias | Alias | Alias | Immediate context |
| CPU / CUDA | Alias | Alias | Alias | 串行后端 |

### 3.4 跨队列同步

- `gpuCreateFence` / `gpuQueueSubmitWithSync` / `gpuQueueSubmitWithWait` / `gpuQueueSubmitWithSignal`
- `gpuSetupComputeToGraphicsSync`：compute 完成后 signal，graphics 侧 wait 的便捷封装
- 设备销毁前会等待所有非 alias 队列 idle

---

## 4. RenderGraph

基于逻辑资源、pass 依赖与 compile 期 barrier 推导的帧图 API（`gpu/rendergraph/gpu_render_graph.h`）。

### 4.1 Pass 类型

| Pass | 枚举 | 默认队列 |
|------|------|----------|
| Render | `GPU_GRAPH_PASS_RENDER` | Graphics |
| Compute | `GPU_GRAPH_PASS_COMPUTE` | Compute |
| Copy | `GPU_GRAPH_PASS_COPY` | Transfer |

Copy pass 在 compile 时根据 1 read + 1 write buffer 推导 `gpuCmdCopyBuffer` 参数；也可通过 callback 扩展。

### 4.2 资源

- **Transient**：`gpuGraphCreateTexture` / `gpuGraphCreateBuffer`，compile 时分配，graph 销毁或 reset 时释放
- **Import**：`gpuGraphImportTexture` / `gpuGraphImportBuffer` / `gpuGraphImportSurfaceTexture`
- **访问声明**：`gpuGraphPassRead` / `Write` / `ReadWrite` / `Present`，支持 subresource 级声明

### 4.3 执行模式

```c
typedef enum {
    GPU_GRAPH_EXECUTE_SINGLE_COMMAND_BUFFER = 0,  // 单 CB 顺序执行
    GPU_GRAPH_EXECUTE_PASS_SUBMIT = 1,            // 每 pass 一次 submit
    GPU_GRAPH_EXECUTE_MULTI_QUEUE = 2,            // 按队列分段 + fence
} GpuGraphExecuteMode;
```

- `gpuGraphSetExecuteMode` 设置请求模式；`gpuGraphGetEffectiveExecuteMode` 返回 compile 后的有效模式
- 当后端仅有 alias 队列时，MULTI_QUEUE 会降级为串行执行，并通过 `gpuGraphGetValidationWarning` 给出警告

### 4.4 Barrier 与诊断

- Compile 自动生成 pass 间 resource transition 与 UAV/global barrier
- `gpuGraphGetBarrier` / `gpuGraphGetPassBarrier`：查询 barrier 链（含 subresource、mip、queue ownership）
- `gpuGraphExportDot` / `gpuGraphExportJson`：导出调试图
- Pass GPU 耗时：`gpuGraphSetPassProfiling` + `gpuGraphGetPassGpuDurationMs`

### 4.5 Transient 分配

- 对象池复用：重复 compile 时复用 transient texture/buffer（`gpuGetTransientTexturePoolCount` 等）
- 生命周期重叠分析：`gpuGraphGetResourceLifetime`、`gpuGraphBuildTransientAllocationPlan`
- **Vulkan placed heap**（原型）：`gpuDeviceSupportsTransientHeap` + compile 计划中的 `heapPlaced` 标志

### 4.6 典型用法

```c
GpuGraph graph;
gpuGraphCreate(device, &graph);
gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_MULTI_QUEUE);

GpuGraphResource color = gpuGraphCreateTexture(graph, &desc, "hdr");
GpuGraphResource backbuffer = gpuGraphImportSurfaceTexture(graph, surfaceTex, "swapchain");

GpuGraphPass compute = gpuGraphAddComputePass(graph, "fill");
gpuGraphPassWrite(compute, buf);
gpuGraphPassSetCallback(compute, my_compute_cb, &data);

GpuGraphPass render = gpuGraphAddRenderPass(graph, "forward");
gpuGraphPassSetColorAttachments(render, 1, &ca);
gpuGraphPassSetCallback(render, my_render_cb, &data);

gpuGraphCompile(graph);
gpuGraphExecute(graph, graphicsQueue);
gpuGraphDestroy(graph);
```

---

## 5. Barrier 与 Hazard

### 5.1 手动 Resource State

- `gpuCmdSetBufferState` / `gpuCmdSetTextureState` / `gpuCmdSetTextureSubresourceState`
- `gpuCmdGlobalBarrier`

### 5.2 Hazard 跟踪

- `gpu_hazard.h`：图 compile 与调试路径中的 hazard 分类（`GpuHazardKind`）
- 支持 read-after-write、UAV 链、present 链等场景的 barrier 推导

---

## 6. Compute 绑定

轻量 compute dispatch 辅助（基于 Slang `ShaderCursor`）：

```c
GpuComputeBinding binding;
gpuCreateComputeBinding(device, computePipeline, &binding);

GpuComputePassEncoder cp = gpuCmdBeginComputePass(enc);
gpuComputeBindingDispatch(binding, cp, "gCounter", buffer, 1, 1, 1);
gpuCmdEndComputePass(cp);
```

适用于示例与测试；复杂 bindless / 参数块场景请使用 `gpu_parameter_block` 与 reflection 路径。

---

## 7. Shader、Pipeline、Descriptor

| 模块 | 头文件 | 能力 |
|------|--------|------|
| Shader 编译 | `gpu_shader_compiler.h` | Slang 源码编译为多后端 target |
| Graphics Pipeline | `gpu_pipeline.h` | 顶点/片段、render target 格式 |
| Compute Pipeline | `gpu_pipeline.h` | compute entry |
| Pipeline Layout | `gpu_pipeline_layout.h` | 反射 layout、binding range、layout hash |
| Descriptor Set | `gpu_descriptor_set.h` | pool、allocate、update、按名绑定 |
| Bindless Heap | `gpu_bindless_heap.h` | texture/buffer/sampler slot、mixed heap |
| Pipeline Cache | `gpu_pipeline_cache.h` | 按 layout hash 缓存 |

---

## 8. 进阶能力（Phase 3+）

以下模块已有 API 与示例，成熟度因后端而异：

| 能力 | 头文件 | 示例 |
|------|--------|------|
| Tensor / 矩阵 | `gpu_tensor.h`, `gpu_matrix_pipeline.h` | `15_tensor_matmul`, `20_tensor_data_ops` |
| Neural | `gpu_neural.h` | `16_neural_render` |
| Work Graph | `gpu_work_graph.h` | `17_workgraph` |
| Ray Tracing | `gpu_raytracing.h` | `18_raytracing` |
| Feature Gating | `gpu_feature_gating.h` | `14_feature_gating` |
| 后端切换 | `gpu_backend.h` | `13_backend_switch` |

---

## 9. 示例程序

| 示例 | 说明 |
|------|------|
| `00_window` / `00_window_clear` | 窗口与清屏 |
| `01_device_init` | 设备与资源生命周期 |
| `06_triangle` / `06b_triangle_readback` | 三角形与 readback |
| `07_compute_pipeline` | Compute shader |
| `22_triangle_graph` | RenderGraph 三角形 |
| `23_readback_graph` | Graph + readback |
| `24_sponza_graph` | Sponza 场景 + 帧图（C++） |
| `25_async_compute_graph` | 多队列 async compute + render |
| `13`–`21` | 后端、特性门控、AI、光追等 |

运行前请从 `build/examples/<Config>/` 目录启动，以便加载 shader 与 slang DLL。

---

## 10. 测试套件

```bash
cmake -B build -DGPU_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

| 测试 | 覆盖 |
|------|------|
| `smoke` | 基础分配与句柄复用 |
| `phase1`–`phase3` | 管线、bindless、AI 等阶段性测试 |
| `phaseA` / `phaseB` | 生命周期与 frame context |
| **`phaseC`** | **RenderGraph 全量（C.1–C.40）**：pass、barrier、多队列、copy 校验、transient heap |
| **`phaseD`** | **Layout / descriptor / bindless 反射（D.1–D.19）** |
| `phaseE` | 后端一致性 |

单独运行：

```powershell
.\build\tests\Release\phaseC.exe
.\build\tests\Release\phaseD.exe
```

---

## 11. 已知限制

1. **Descriptor**：mixed bindless heap 与部分传统 set 路径仍在完善中（见 completion plan §2）。
2. **Barrier**：全功能 hazard resolver（subresource alias、跨队列 ownership 诊断）仍在演进。
3. **Transient**：除 Vulkan placed heap 原型外，通用 heap allocator / 资源 aliasing 尚未完全落地。
4. **WebGPU / D3D11**：无真实多队列，async compute 无硬件并行收益。
5. **示例 25**：Win32 子系统应用；CI / MSVC 下已链接 `CONSOLE` 子系统以便查看 `printf`。

---

## 12. 相关文档

- [rhi_rendergraph_completion_plan.md](rhi_rendergraph_completion_plan.md) — RHI / RenderGraph 改造路线
- [production_ready_rhi.md](production_ready_rhi.md) — 生产就绪标准
- [api.md](api.md) — API 设计笔记
- [README.md](../README.md) — 构建与快速上手
