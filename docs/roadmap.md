
## 5. 开发 Roadmap

### 5.1 三阶段定制路线图

开发过程将分为三个关键阶段，从基础架构的建立逐步演进到前沿特性的支持。

#### 5.1.1 第一阶段：核心基础设施（M0 - M3）

本阶段的目标是建立稳定的 Shader-Centric 架构底座，实现资源 Handle 化。

*   **IBindlessHeap 实现**：建立全局唯一的描述符堆管理模式，支持 Texture 和 Buffer 的全局索引访问 [1]。
*   **RHICapabilities 系统**：构建独立的功能特性判断系统，实现硬件能力的对齐与 Feature Gating [3]。
*   **ShaderObject 增强**：扩展 Slang-RHI 的 `IShaderObject` 机制，实现参数块（Parameter Block）的自动绑定与嵌套偏移计算 [6]。
*   **Slang Reflection 集成**：将 Slang 反射信息转化为引擎内部的 `GpuTypeInfo`，支持自动化 UI 绑定与 Agent 识别 [3]。

#### 5.1.2 第二阶段：现代渲染特性（M4 - M6）

本阶段侧重于提升渲染效率与异步计算能力。

*   **Mesh Shader 支持**：实现 `IMeshPipeline` 接口，适配现代几何流水线 [1]。
*   **Multi Queue 架构**：Fork 并重构 Slang-RHI 的命令队列模型，引入独立的 Compute 和 Transfer 队列，支持异步计算 [2]。
*   **Sparse Resource**：实现稀疏资源与虚拟贴图技术，支持超大规模场景的资源流送 [1]。

#### 5.1.3 第三阶段：AI 与前沿技术（M7 - M12）

本阶段旨在补齐 AI 推理加速与 GPU 驱动的复杂任务调度能力。

*   **Cooperative Matrix**：支持硬件加速的矩阵计算接口，用于神经渲染与 AI 推理加速 [1]。
*   **Work Graph**：引入 GPU Task Graph 机制，实现 GPU 驱动的复杂渲染任务调度 [8]。
*   **Ray Tracing 模块**：完善光线追踪管线支持，实现跨后端的 RT 接口对齐 [1]。

### 5.2 里程碑规划

| 里程碑 | 交付物 | 验收标准 | 时间节点 |
| :--- | :--- | :--- | :--- |
| **M1: Foundation** | GpuDevice, BindlessHeap, GpuTypeInfo | 通过基础反射测试，实现 Bindless 纹理采样 | M3 |
| **M2: Efficiency** | AsyncComputeQueue, MeshPipeline | 异步计算性能提升 >15%，Mesh Shader 渲染正确 | M6 |
| **M3: Intelligence** | GpuTensor, WorkGraph, RTModule | 成功运行神经渲染 Demo，支持 GPU 驱动裁剪 | M12 |

### 5.3 后端适配优先级

系统将按照硬件成熟度与生态覆盖率逐步适配各平台后端：

1.  **Vulkan**：首选成熟后端，作为所有新特性的首发平台，利用 VMA 管理内存 [8]。
2.  **D3D12**：重点适配 Agility SDK 特性，如 Work Graphs，利用 D3D12MA 管理内存 [8]。
3.  **Metal**：针对移动端优化，重点解决 Bindless 模拟与 Tile-based 架构适配 [6]。
4.  **WebGPU**：优先保证 Tensor 计算接口兼容性，为 Web 端 AI 提供基础支持 [11]。
