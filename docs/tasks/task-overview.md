# GPU 抽象层 - 任务总览

## 项目概览

基于 Slang/Slang-RHI 构建通用 GPU 抽象层，核心哲学为 **Shader-Centric**，三大技术投资方向：**Slang Reflection + ShaderObject + Bindless**。平台层使用 **SDL** 统一窗口、输入与 Surface 创建。

## 三阶段路线图

```
Phase 0: 平台与构建 (M0)  ──→  Phase 1: 核心基础设施 (M1-M3)  ──→  Phase 2: 现代渲染特性 (M4-M6)  ──→  Phase 3: AI与前沿技术 (M7-M12)
         SDL/Swapchain/CI          L0-L4                              L5-L6                              L7-L9
```

## 架构层级与任务映射

| 层级 | 名称 | Phase | 任务文档 | 核心交付物 |
|:---|:---|:---|:---|:---|
| **L-1** | **Platform** | 0 | [phase0-platform.md](phase0-platform.md) | SDL 窗口、Surface、Swapchain、CI |
| **L0** | Runtime | 1 | [phase1-foundation.md](phase1-foundation.md) | GpuDevice, GpuBuffer, GpuTexture C API |
| **L1** | Shader System | 1 | [phase1-foundation.md](phase1-foundation.md) | ShaderCompiler, SlangModule |
| **L2** | Reflection | 1 | [phase1-foundation.md](phase1-foundation.md) | GpuTypeInfo, ReflectionCache |
| **L3** | Resource | 1 | [phase1-foundation.md](phase1-foundation.md) | Handle 系统, StreamingManager, GpuFence |
| **L4** | Bindless | 1 | [phase1-foundation.md](phase1-foundation.md) | GpuBindlessHeap, DescriptorIndex |
| **L5** | Pipeline | 2 | [phase2-rendering.md](phase2-rendering.md) | GraphicsPipeline, ComputePipeline |
| **L6** | Layout | 2 | [phase2-rendering.md](phase2-rendering.md) | ParameterBlock, ResourceLayout |
| **L7** | Backend | 3 | [phase3-ai-frontier.md](phase3-ai-frontier.md) | Vulkan/D3D12/Metal/WebGPU 适配 |
| **L8** | Capability | 3 | [phase3-ai-frontier.md](phase3-ai-frontier.md) | GpuCapabilities, FeatureGating |
| **L9** | Future AI | 3 | [phase3-ai-frontier.md](phase3-ai-frontier.md) | GpuTensor, GpuMatrixPipeline |

## 里程碑验收标准

| 里程碑 | 时间 | 验收标准 |
|:---|:---|:---|
| **M0: Platform** | M0 | SDL 窗口创建，Swapchain 呈现清屏画面 |
| **M1: Foundation** | M3 | 通过基础反射测试，实现 Bindless 纹理采样，屏幕呈现三角形 |
| **M2: Efficiency** | M6 | 异步计算性能提升 >15%，Mesh Shader 渲染正确 |
| **M3: Intelligence** | M12 | 成功运行神经渲染 Demo，支持 GPU 驱动裁剪 |

## 后端适配优先级

1. **Vulkan** - 首选成熟后端，所有新特性首发平台
2. **D3D12** - 重点适配 Agility SDK (Work Graphs)
3. **Metal** - 移动端优化，Argument Buffers 模拟 Bindless
4. **WebGPU** - 优先保证 Tensor 计算接口兼容性

## 任务依赖关系

```
L-1 Platform (SDL/Swapchain)
  │
  └──→ L0 Runtime ──→ L3 Resource ──→ L4 Bindless ──→ L5 Pipeline
                            │                                    │
                            │         L1 Shader ──→ L2 Reflection ──→ L6 Layout
                            │
                            └──→ GpuFence (基础同步, Phase 1 引入)
                                                │
                                          L7 Backend (各后端适配)
                                                │
                                          L8 Capability
                                                │
                                          L9 Future AI
```

## 交叉关注点

- **线程安全**: Device 级线程安全, CommandBuffer 独占, Queue 提交同步
- **ABI 稳定**: PIMPL + C API, Handle 化资源管理, 世代索引防悬空
- **错误处理**: 统一 GpuResult 状态码 + `gpuGetLastError()` 详细信息回调, 调试层可选启用
- **内存管理**: 子分配策略 (VMA/D3D12MA), 延迟释放, GPU 完成后回收
- **资源屏障**: 自动或手动资源状态转换, 覆盖典型 compute→graphics / upload→read 场景

## 架构边界 (不包含)

本抽象层 **明确不包含** 以下内容（属于上层 Framework）：
- RenderGraph / Frame Graph
- 具体材质业务逻辑 / 材质编辑器
- 场景树管理 / ECS
- 具体渲染器实现 (Forward / Deferred / GI)
