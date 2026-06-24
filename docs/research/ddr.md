# GPU Driven Rendering 中的 Shader 宏策略与 Bindless 资源绑定实现研究报告

## 1. Alex Tardif Bindless Programming 方案分析

Alex Tardif 的 Bindless 方案核心在于消除传统的 CPU 逐资源绑定（BindConstantBuffer, BindTextures），转而利用全局描述符堆（Descriptor Heap）和根常量（Root Constants）实现 GPU 驱动的数据访问 [1]。

### 1.1 HLSL 资源声明与宏定义策略
在 Shader Model 6.6+ 环境下，Tardif 提倡直接使用内置的全局堆对象。通过宏定义，可以将传统的寄存器绑定抽象为索引访问：

```hlsl
// 传统绑定
// Texture2D g_MainTex : register(t0);

// Bindless 宏抽象
#define GetResource(index) ResourceDescriptorHeap[index]
#define GetSampler(index) SamplerDescriptorHeap[index]

// 类型安全封装示例
#define DECLARE_TEXTURE2D(name) uint name##Index
#define SAMPLE_TEXTURE2D(name, samplerName, uv) \
    ResourceDescriptorHeap[name##Index].Sample(SamplerDescriptorHeap[samplerName##Index], uv)
```

### 1.2 Draw ID 与 Instance ID 的传递
在 GPU 驱动管线中，`ExecuteIndirect` 是核心。Tardif 方案通常将 `DrawID` 作为根常量（Root Constant）传递，或者利用 `SV_InstanceID` 结合 `baseInstance` 偏移来定位当前绘制对象的数据 [1][5]。

### 1.3 访问模式：StructuredBuffer vs ByteAddressBuffer
Tardif 在其 D3D12Lite 框架中更倾向于使用 `ByteAddressBuffer`。
*   **StructuredBuffer<T>**：类型安全，但在处理具有不同步长或复杂对齐要求的混合数据时灵活性较差。
*   **ByteAddressBuffer**：提供原始内存访问，通过 `.Load<T>(offset)` 模式读取。这种方式在 Bindless 架构中被视为“万能缓冲区”，能够高效处理变长材质数据 [5]。

### 1.4 Root Signature 与 Descriptor Heap 组织
根签名（Root Signature）不再包含大量的描述符表，而是配置为允许直接索引堆。
*   **Flags**: 必须开启 `D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED`。
*   **组织方式**: 整个场景的所有 SRV/UAV 存放在一个巨大的描述符堆中（通常为 1,000,000 级），Shader 通过 32 位索引直接访问 [4]。

## 2. JorenJoestar HFX Shader Effect Language 详解

HFX (Hydra Effects) 是由 JorenJoestar 设计的一种数据驱动 Shader 语言，旨在解决 Shader 变体爆炸和资源绑定硬编码问题 [2][3]。

### 2.1 语法设计与 Layout 块
HFX 采用类似 Unity ShaderLab 的结构，通过 `layout` 块显式定义资源更新频率和绑定关系：

```hfx
shader ForwardLit {
    properties {
        _MainTex("Albedo", Texture2D) = "white" {}
        _Color("Main Color", Vector) = (1,1,1,1)
    }
    layout {
        list Local {
            cbuffer MaterialConstants;
            texture2D _MainTex;
        }
        list Frame {
            cbuffer FrameConstants;
        }
    }
    pass Main {
        resources = [Frame, Local]
        vertex = VS_Main
        fragment = PS_Main
    }
}
```

### 2.2 Permutation 宏生成策略
HFX 通过在 `glsl` 或 `hlsl` 代码块中使用预处理器定义（如 `VERTEX`, `FRAGMENT`）来拆分阶段，并根据 `properties` 中的开关自动生成宏组合。C++ 端会根据这些宏编译出不同的二进制变体（BHFX） [3][8]。

### 2.3 C++ 代码生成实现
HFX 解析器生成 AST 后，会自动化生成对应的 C++ 结构体，确保 CPU 端的常量缓冲区布局与 GPU 端完全一致，避免手动对齐带来的错误 [2]。

## 3. 跨平台 Bindless 宏抽象层实现

为了统一 DX12、Vulkan 和 Metal 的差异，需要建立一套抽象宏层 [6]。

| 特性 | DirectX 12 (HLSL SM6.6) | Vulkan (GLSL/SPIR-V) | Metal (MSL) |
| :--- | :--- | :--- | :--- |
| 核心机制 | ResourceDescriptorHeap | Descriptor Indexing | Argument Buffers |
| 索引限制 | 32-bit uint | nonuniformEXT() | [[id(n)]] |
| 声明方式 | 无需显式声明 | 巨大数组 `textures[]` | 结构体指针 |

### 3.1 统一宏策略示例
```hlsl
#if defined(DX12)
    #define BINDLESS_TEX2D(idx) ResourceDescriptorHeap[idx]
#elif defined(VULKAN)
    layout(set=0, binding=0) uniform texture2D globalTextures[];
    #define BINDLESS_TEX2D(idx) globalTextures[nonuniformEXT(idx)]
#elif defined(METAL)
    struct ArgBuffer { texture2d<float> texs [[id(0)]][1000]; };
    #define BINDLESS_TEX2D(idx) args->texs[idx]
#endif
```

## 4. 资源索引类型安全封装

为了防止 `uint` 索引被误用，通常采用结构体封装模式 [5]：

```hlsl
struct TextureHandle { uint index; };
struct BufferHandle  { uint index; };

// 类型安全访问函数
Texture2D GetTexture(TextureHandle h) {
    return ResourceDescriptorHeap[h.index];
}

// 进阶：带版本校验的 Handle
struct SafeHandle {
    uint GetIndex() { return data & 0xFFFFF; } // 前20位为索引
    uint GetType()  { return data >> 20; }    // 后12位为类型标识
    uint data;
};
```

## 5. Shader Permutation 策略

### 5.1 编译时策略 (Static Switches)
利用宏定义生成大量 PSOs。适用于性能关键路径，如光照模型切换。通过引擎工具链（如 HFX 编译器）在构建期剔除无效组合 [7]。

### 5.2 运行时策略 (Dynamic Branching)
在 GPU Driven 管线中，为了减少 PSO 切换开销，常将静态宏替换为基于材质 ID 的动态分支。虽然增加了寄存器压力，但能显著提升 Indirect Draw 的批处理效率 [4][7]。

## 6. Indirect Draw 与资源索引协调方案

在 GPU 驱动管线中，资源索引的解析遵循以下链路 [6][9]：
1.  **Compute Shader**: 进行剔除，将可见实例的 `InstanceID` 和 `MaterialID` 写入 `IndirectArgumentBuffer`。
2.  **ExecuteIndirect**: 触发绘制，Shader 通过 `SV_InstanceID` 获取实例数据。
3.  **数据检索**: `InstanceData` -> `MaterialIndex` -> `MaterialStruct` -> `TextureIndex`。
4.  **最终采样**: 使用 `ResourceDescriptorHeap[TextureIndex]` 获取纹理。

### 6.1 内存布局结构体定义
```cpp
struct MaterialData {
    uint albedoIdx;
    uint normalIdx;
    float roughness;
    float metallic;
    // 严格 16 字节对齐
};

struct InstanceData {
    float4x4 worldMatrix;
    uint materialIdx;
    uint pad0, pad1, pad2;
};
```

## 7. 完整代码示例集

### 7.1 HLSL SM6.6 完整 Bindless 示例 (Root Constant + DrawID)

```hlsl
// ==================== 根常量定义 ====================
struct DrawData {
    uint vertexBufferIdx;  // 顶点缓冲区在堆中的索引
    uint textureIdx;       // 纹理在堆中的索引
    uint materialIdx;      // 材质数据索引
    uint drawID;           // 当前绘制调用的唯一ID
};

ConstantBuffer<DrawData> g_DrawData : register(b0);

// ==================== 顶点着色器：Bindless 顶点拉取 ====================
struct VSInput {
    uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;
};

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint materialIdx : MATERIAL_ID;
};

PSInput VSMain(VSInput input) {
    // 从全局堆直接访问顶点数据
    StructuredBuffer<float3> posBuffer = ResourceDescriptorHeap[g_DrawData.vertexBufferIdx];
    StructuredBuffer<float2> uvBuffer  = ResourceDescriptorHeap[g_DrawData.vertexBufferIdx + 1];
    
    PSInput output;
    output.position = float4(posBuffer[input.vertexID], 1.0f);
    output.uv = uvBuffer[input.vertexID];
    output.materialIdx = g_DrawData.materialIdx;
    return output;
}

// ==================== 像素着色器：Bindless 纹理采样 ====================
float4 PSMain(PSInput input) : SV_Target {
    // 类型安全的纹理访问
    Texture2D albedoTex = ResourceDescriptorHeap[g_DrawData.textureIdx];
    SamplerState linearSampler = SamplerDescriptorHeap[0];
    
    // 非均匀索引场景（材质ID因像素而异）
    Texture2D materialTex = ResourceDescriptorHeap[NonUniformResourceIndex(input.materialIdx)];
    
    float4 albedo = albedoTex.Sample(linearSampler, input.uv);
    return albedo;
}
```

### 7.2 Vulkan GLSL Bindless 完整示例

```glsl
#version 450
#extension GL_EXT_nonuniform_qualifier : require

// ==================== Bindless 纹理数组声明 ====================
layout(set = 0, binding = 0) uniform sampler2D bindlessTextures[];

// ==================== Push Constants (等价于 Root Constants) ====================
layout(push_constant) uniform PushConstants {
    uint textureIndex;
    uint materialIndex;
    uint drawID;
} pc;

// ==================== 输入/输出 ====================
layout(location = 0) in vec2 inUV;
layout(location = 1) flat in int inMaterialID;  // flat = nointerpolation

layout(location = 0) out vec4 outColor;

void main() {
    // 均匀索引：整个绘制调用使用相同索引
    vec4 baseColor = texture(bindlessTextures[pc.textureIndex], inUV);
    
    // 非均匀索引：不同像素可能使用不同纹理
    // nonuniformEXT 确保硬件正确处理发散索引
    vec4 materialColor = texture(
        bindlessTextures[nonuniformEXT(inMaterialID)], 
        inUV
    );
    
    outColor = baseColor * materialColor;
}
```

### 7.3 Metal MSL Argument Buffer Bindless 示例

```cpp
#include <metal_stdlib>
using namespace metal;

// ==================== Argument Buffer 结构体 ====================
struct BindlessResources {
    array<texture2d<float>, 1024> textures [[id(0)]];
    array<sampler, 16> samplers [[id(1024)]];
};

struct MaterialData {
    uint albedoIdx;
    uint normalIdx;
    float roughness;
    float metallic;
};

// ==================== Fragment Shader ====================
fragment float4 fragmentMain(
    VertexOut in [[stage_in]],
    constant BindlessResources& resources [[buffer(0)]],
    constant MaterialData& material [[buffer(1)]]
) {
    // 通过 Argument Buffer 访问 Bindless 纹理
    float4 albedo = resources.textures[material.albedoIdx].sample(
        resources.samplers[0], 
        in.uv
    );
    
    float3 normal = resources.textures[material.normalIdx].sample(
        resources.samplers[0], 
        in.uv
    ).xyz * 2.0 - 1.0;
    
    return albedo;
}
```

### 7.4 GPU Driven Visibility Buffer 材质获取着色器

```hlsl
// ==================== 全局 Bindless 资源 ====================
StructuredBuffer<uint> g_VisibilityBuffer;
StructuredBuffer<Vertex> g_GlobalVertices;
StructuredBuffer<uint> g_GlobalIndices;
StructuredBuffer<MaterialData> g_Materials;

// ==================== 计算着色器：延迟材质获取 ====================
[numthreads(8, 8, 1)]
void MaterialFetchCS(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;
    
    // 1. 解包 Visibility Buffer 数据
    uint packedData = g_VisibilityBuffer[pixelCoord.y * screenWidth + pixelCoord.x];
    uint triangleID = packedData & 0x00FFFFFF;  // 24位三角形ID
    uint instanceID = packedData >> 24;          // 8位实例ID
    
    // 2. 获取三角形顶点
    uint baseIdx = triangleID * 3;
    uint i0 = g_GlobalIndices[baseIdx + 0];
    uint i1 = g_GlobalIndices[baseIdx + 1];
    uint i2 = g_GlobalIndices[baseIdx + 2];
    
    Vertex v0 = g_GlobalVertices[i0];
    Vertex v1 = g_GlobalVertices[i1];
    Vertex v2 = g_GlobalVertices[i2];
    
    // 3. 计算重心坐标
    float3 bary = CalculateBarycentric(pixelCoord, v0.screenPos, v1.screenPos, v2.screenPos);
    
    // 4. 插值属性
    float2 uv = v0.uv * bary.x + v1.uv * bary.y + v2.uv * bary.z;
    float3 normal = normalize(v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z);
    
    // 5. Bindless 材质获取
    uint materialIdx = GetMaterialIndex(instanceID);
    MaterialData mat = g_Materials[materialIdx];
    
    Texture2D albedoTex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.albedoIdx)];
    Texture2D normalTex = ResourceDescriptorHeap[NonUniformResourceIndex(mat.normalIdx)];
    
    float4 albedo = albedoTex.SampleLevel(g_LinearSampler, uv, CalculateMipLevel(uv));
    
    // 6. 写入 G-Buffer 或直接光照计算
    g_OutputAlbedo[pixelCoord] = albedo;
    g_OutputNormal[pixelCoord] = float4(normal, mat.roughness);
}
```

### 7.5 HFX 完整 Shader Effect 示例

```hfx
shader PBRLit {
    // ==================== 艺术家可编辑属性 ====================
    properties {
        _AlbedoMap("Albedo", Texture2D) = "white" {}
        _NormalMap("Normal", Texture2D) = "bump" {}
        _Metallic("Metallic", Range(0,1)) = 0.0
        _Roughness("Roughness", Range(0,1)) = 0.5
        _Color("Tint Color", Color) = (1,1,1,1)
    }
    
    // ==================== 资源布局定义 ====================
    layout {
        // 按更新频率分组（类似 Vulkan Descriptor Set）
        list PerFrame {
            cbuffer FrameConstants;      // 相机、时间等
        }
        
        list PerMaterial {
            cbuffer MaterialConstants;   // _Metallic, _Roughness, _Color
            texture2D _AlbedoMap;
            texture2D _NormalMap;
        }
        
        list PerObject {
            cbuffer ObjectConstants;     // World Matrix
        }
    }
    
    // ==================== GLSL 代码块 ====================
    glsl PBRShading {
        #if defined(VERTEX)
        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec2 inUV;
        layout(location = 2) in vec3 inNormal;
        
        void main() {
            // 顶点变换逻辑
        }
        #endif
        
        #if defined(FRAGMENT)
        void main() {
            // PBR 光照计算
        }
        #endif
    }
    
    // ==================== 渲染 Pass 定义 ====================
    pass MainPass {
        resources = [PerFrame, PerMaterial, PerObject]
        vertex = PBRShading
        fragment = PBRShading
        
        // 渲染状态
        depth_test = less_equal
        depth_write = true
        cull_mode = back
    }
    
    pass ShadowPass {
        resources = [PerFrame, PerObject]
        vertex = ShadowCaster
        fragment = ShadowCaster
        
        depth_test = less
        depth_write = true
        color_write = false
    }
}
```

### 7.6 跨平台 Bindless 宏抽象层完整实现

```hlsl
// ==================== Platform Detection ====================
#if defined(__HLSL_VERSION) && __HLSL_VERSION >= 2021
    #define PLATFORM_DX12_SM66 1
#elif defined(VULKAN)
    #define PLATFORM_VULKAN 1
#elif defined(__METAL_VERSION__)
    #define PLATFORM_METAL 1
#endif

// ==================== Type-Safe Handle Definitions ====================
struct TextureHandle  { uint index; };
struct BufferHandle   { uint index; };
struct SamplerHandle  { uint index; };

// ==================== Platform-Specific Resource Access ====================
#if PLATFORM_DX12_SM66
    // DX12 SM6.6: 直接堆访问
    #define BINDLESS_TEXTURE2D(handle) \
        ResourceDescriptorHeap[handle.index]
    
    #define BINDLESS_BUFFER(handle, Type) \
        ((StructuredBuffer<Type>)ResourceDescriptorHeap[handle.index])
    
    #define BINDLESS_SAMPLER(handle) \
        SamplerDescriptorHeap[handle.index]
    
    #define NONUNIFORM(handle) \
        NonUniformResourceIndex(handle.index)

#elif PLATFORM_VULKAN
    // Vulkan: 需要显式数组声明
    layout(set = 0, binding = 0) uniform texture2D g_Textures[];
    layout(set = 0, binding = 1) uniform sampler g_Samplers[];
    layout(set = 1, binding = 0) buffer GlobalBuffers { uint data[]; } g_Buffers[];
    
    #define BINDLESS_TEXTURE2D(handle) \
        g_Textures[handle.index]
    
    #define BINDLESS_SAMPLER(handle) \
        g_Samplers[handle.index]
    
    #define NONUNIFORM(handle) \
        nonuniformEXT(handle.index)

#elif PLATFORM_METAL
    // Metal: 通过 Argument Buffer 访问
    // 假设 g_Args 是绑定的 Argument Buffer
    #define BINDLESS_TEXTURE2D(handle) \
        g_Args.textures[handle.index]
    
    #define BINDLESS_SAMPLER(handle) \
        g_Args.samplers[handle.index]
    
    #define NONUNIFORM(handle) \
        (handle.index)  // Metal 自动处理非均匀访问
#endif

// ==================== 统一采样接口 ====================
#define SAMPLE_BINDLESS(texHandle, sampHandle, uv) \
    BINDLESS_TEXTURE2D(texHandle).Sample(BINDLESS_SAMPLER(sampHandle), uv)

#define SAMPLE_BINDLESS_NONUNIFORM(texHandle, sampHandle, uv) \
    BINDLESS_TEXTURE2D(NONUNIFORM(texHandle)).Sample(BINDLESS_SAMPLER(sampHandle), uv)
```

### 7.7 ExecuteIndirect 与材质 ID 的命令签名布局

```cpp
// ==================== C++ 端：Indirect Command 结构体 ====================
struct IndirectDrawCommand {
    uint32_t materialID;           // Root Constant: 材质索引
    uint32_t objectID;             // Root Constant: 对象索引
    D3D12_DRAW_INDEXED_ARGUMENTS drawArgs;  // 标准绘制参数
};

// ==================== 命令签名创建 ====================
D3D12_INDIRECT_ARGUMENT_DESC argDescs[3] = {};

// 参数0: 材质ID (Root Constant)
argDescs[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
argDescs[0].Constant.RootParameterIndex = 0;
argDescs[0].Constant.DestOffsetIn32BitValues = 0;
argDescs[0].Constant.Num32BitValuesToSet = 1;

// 参数1: 对象ID (Root Constant)
argDescs[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
argDescs[1].Constant.RootParameterIndex = 0;
argDescs[1].Constant.DestOffsetIn32BitValues = 1;
argDescs[1].Constant.Num32BitValuesToSet = 1;

// 参数2: 绘制参数
argDescs[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
sigDesc.ByteStride = sizeof(IndirectDrawCommand);
sigDesc.NumArgumentDescs = 3;
sigDesc.pArgumentDescs = argDescs;

device->CreateCommandSignature(&sigDesc, rootSignature, IID_PPV_ARGS(&commandSignature));
```

## 8. 性能优化建议

### 8.1 描述符管理
- **描述符池化**: 预分配大型描述符堆（100万级），避免运行时扩容
- **分层更新**: 按更新频率划分 Descriptor Set（PerFrame / PerMaterial / PerObject）
- **延迟释放**: 使用环形缓冲延迟释放已提交的描述符，防止 GPU 访问已回收资源

### 8.2 Shader Permutation 管理
- **编译时剔除**: 构建工具自动分析资产依赖，剔除未使用的变体组合
- **PSO 预热**: 在加载画面异步编译常用 PSO 组合
- **动态分支阈值**: 当变体数 < 8 时使用静态宏，> 8 时考虑动态分支

### 8.3 Indirect Draw 批处理
- **材质排序**: GPU 剔除后按 MaterialID 排序，最大化 PSO 连续性
- **Merge Pass**: 合并相同 PSO 的 Indirect Draw 到单个 Multi-Draw-Indirect 调用

## 参考文献

[1] [alextardif.com - Bindless Programming (2020-01-12)](https://alextardif.com/BindlessProgramming.html)


[2] [jorenjoestar.github.io - Writing a Shader Effect Language - Part 1 (2019-09-25)](https://jorenjoestar.github.io/post/writing_shader_effect_language_1/)


[3] [jorenjoestar.github.io - Writing a Shader Effect Language - Part 3 (2019-12-10)](https://jorenjoestar.github.io/post/writing_shader_effect_language_3/)


[4] [microsoft.com - HLSL Shader Model 6.6 Introduction (2021-04-20)](https://devblogs.microsoft.com/directx/announcing-hlsl-shader-model-6-6/)


[5] [traverseresearch.nl - Bindless Rendering Demystified (2022-05-15)](https://traverseresearch.nl/posts/bindless-rendering-demystified/)


[6] [vkguide.dev - GPU Driven Engines (2021-03-10)](https://vkguide.dev/docs/gpudriven/gpu_driven_engines/)


[7] [nvidia.com - GPU-Driven Rendering Pipelines (2019-03-18)](https://developer.nvidia.com/blog/introduction-turing-mesh-shaders/)


[8] [github.com - JorenJoestar/DataDrivenRendering Repository](https://github.com/JorenJoestar/DataDrivenRendering)


[9] [apple.com - Metal Argument Buffers Tier 2 Guide](https://developer.apple.com/documentation/metal/buffers/about_argument_buffers)
---

## 9. 本仓库 GPU 层 DDR 框架落地方案

这里的 DDR 建议明确为 **Data Driven Rendering**。它不只是 bindless shader 宏，也不是某个 Sponza demo 的渲染代码，而应该成为 `gpu` 层之上的一套框架：上层提交场景数据、材质数据、渲染技术描述和 pass 配置，DDR 层把这些数据编译成 pipeline、bindless slot、draw packet、indirect command 和 render graph pass。

当前仓库已经具备几个可复用基础：

- `GpuFeatureInfo` 三态能力表，可声明 bindless、descriptor indexing、multi queue、timeline fence 等能力。
- `GpuBindlessHeap` 已有 texture view、buffer、RW descriptor handle 注册能力。
- `GpuPipelineLayout` 已能从 Slang program reflection 导出 descriptor set、binding、push constant、layout hash。
- `GpuGraph` 已提供 render/compute/copy pass、import/create resource、read/write 依赖、DOT/JSON 导出。
- `examples/24_sponza_graph` 已经证明了 render graph + bindless + 多 pass + Sponza 的路径可以跑通。

因此 DDR 不应重写 RHI，而应作为 `src/gpu/ddr/` 下的一层框架，复用以上能力。

### 9.1 目标与非目标

目标：

1. 用数据描述渲染技术，而不是在 demo 代码里手写 pass、pipeline、bind。
2. 用 bindless/resource table 统一材质、纹理、buffer、sampler 和 per-frame data。
3. 支持 GPU driven path：CPU 构建 draw packet，GPU compute 做 culling/binning，最终走 indirect draw 或后端可用的替代路径。
4. 与 `GpuGraph` 集成，DDR pass 自动声明读写资源，graph 负责编译 barrier 和执行顺序。
5. 与 Slang reflection 集成，pipeline layout、resource layout 和 shader 参数由反射驱动。
6. 支持 fallback：没有 native indirect/multi-draw 时允许 CPU submit path，但必须通过 feature table 暴露。

非目标：

1. DDR 层不直接暴露 D3D12/Vulkan/Metal 原生对象。
2. DDR 层不替代 asset pipeline，但要定义可被 asset pipeline 生成的中间数据结构。
3. DDR 层第一阶段不实现完整 material editor 或 shader effect language parser，先用 C API/JSON-like desc 表达。
4. DDR 层不绕过 `GpuGraph` 自己管理 frame pass barrier。

### 9.2 推荐模块划分

建议新增目录：

```text
src/gpu/ddr/
  gpu_ddr.h
  gpu_ddr_types.h
  gpu_ddr_context.h/.cpp
  gpu_ddr_resource_table.h/.cpp
  gpu_ddr_material.h/.cpp
  gpu_ddr_scene.h/.cpp
  gpu_ddr_technique.h/.cpp
  gpu_ddr_compiler.h/.cpp
  gpu_ddr_indirect.h/.cpp
  gpu_ddr_graph.h/.cpp
  gpu_ddr_debug.h/.cpp
```

依赖方向：

```text
gpu_ddr_context
  -> feature_gating
  -> bindless heap
  -> pipeline layout/reflection
  -> render graph
  -> core resource/buffer/texture

render graph 不依赖 ddr
rhi core 不依赖 ddr
examples 依赖 ddr
```

这样 DDR 是 RHI 上层框架，不会把 RHI core 搅成场景渲染器。

### 9.3 核心数据模型

#### Resource Table

DDR 需要一个全局或 per-world 的资源表，统一登记 GPU 可见资源。它不是简单的 `GpuBindlessHeap` 包装，而是带类型、生命周期、debug name、版本信息的 resource registry。

```c
typedef uint32_t GpuDdrResourceId;
typedef uint32_t GpuDdrMaterialId;
typedef uint32_t GpuDdrMeshId;
typedef uint32_t GpuDdrInstanceId;
typedef uint32_t GpuDdrTechniqueId;

typedef enum {
    GPU_DDR_RESOURCE_TEXTURE = 0,
    GPU_DDR_RESOURCE_BUFFER = 1,
    GPU_DDR_RESOURCE_SAMPLER = 2,
    GPU_DDR_RESOURCE_TEXTURE_UAV = 3,
    GPU_DDR_RESOURCE_BUFFER_UAV = 4,
} GpuDdrResourceKind;

typedef struct {
    GpuDdrResourceKind kind;
    GpuHandle handle;
    uint32_t bindlessIndex;
    uint32_t generation;
    const char* debugName;
} GpuDdrResourceRef;
```

原则：

- CPU 侧永远使用 `GpuDdrResourceId`，shader 侧使用 `bindlessIndex`。
- 资源销毁时 DDR table 必须 invalidate 对应 resource id 和 bindless slot。
- 材质不直接保存 `GpuTextureHandle`，只保存 `GpuDdrResourceId` 或最终编译后的 bindless index。

#### Material Schema 与 Material Instance

Material schema 描述 shader 期望的数据布局，material instance 保存具体参数和纹理引用。

```c
typedef enum {
    GPU_DDR_PARAM_FLOAT = 0,
    GPU_DDR_PARAM_FLOAT2 = 1,
    GPU_DDR_PARAM_FLOAT3 = 2,
    GPU_DDR_PARAM_FLOAT4 = 3,
    GPU_DDR_PARAM_UINT = 4,
    GPU_DDR_PARAM_TEXTURE2D = 5,
    GPU_DDR_PARAM_SAMPLER = 6,
} GpuDdrParamType;

typedef struct {
    const char* name;
    GpuDdrParamType type;
    uint32_t offset;
    uint32_t size;
} GpuDdrMaterialParamDesc;

typedef struct {
    const char* name;
    uint32_t paramCount;
    const GpuDdrMaterialParamDesc* params;
    uint32_t constantDataSize;
} GpuDdrMaterialSchemaDesc;
```

推荐 material GPU layout：

```c
typedef struct GpuDdrMaterialGpu {
    uint32_t baseColorTexture;
    uint32_t normalTexture;
    uint32_t metallicRoughnessTexture;
    uint32_t sampler;
    float baseColorFactor[4];
    float metallicFactor;
    float roughnessFactor;
    uint32_t flags;
    uint32_t pad0;
} GpuDdrMaterialGpu;
```

第一阶段不要做复杂 shader effect parser。可以先让 `GpuDdrMaterialSchemaDesc` 由 C/C++ 或离线工具生成，后续再接 HFX/JSON/YAML。

#### Mesh / Draw Packet / Instance

DDR 的核心不是材质绑定，而是把场景压成 GPU 可消费的 draw packet。

```c
typedef struct {
    GpuBufferHandle vertexBuffer;
    GpuBufferHandle indexBuffer;
    uint32_t vertexStride;
    uint32_t indexType;
    uint32_t indexCount;
    uint32_t firstIndex;
    int32_t vertexOffset;
} GpuDdrMeshDesc;

typedef struct {
    GpuDdrMeshId mesh;
    GpuDdrMaterialId material;
    float world[16];
    uint32_t visibilityMask;
    uint32_t flags;
} GpuDdrInstanceDesc;

typedef struct {
    uint32_t meshId;
    uint32_t materialId;
    uint32_t instanceId;
    uint32_t pipelineKey;
    uint32_t firstIndex;
    uint32_t indexCount;
    int32_t vertexOffset;
    uint32_t firstInstance;
} GpuDdrDrawPacket;
```

CPU fallback path 可以遍历 draw packet 调 `drawIndexed`。GPU driven path 则将 draw packet 上传到 buffer，由 compute shader 做 culling/compaction，写 indirect command buffer。

### 9.4 Technique / Pass 描述

DDR 要表达“一个渲染技术包含哪些 pass，每个 pass 用什么 shader、读写哪些 graph resource、使用什么 render state”。

```c
typedef enum {
    GPU_DDR_PASS_RENDER = 0,
    GPU_DDR_PASS_COMPUTE = 1,
    GPU_DDR_PASS_COPY = 2,
} GpuDdrPassKind;

typedef struct {
    const char* name;
    GpuDdrPassKind kind;
    const char* vertexEntry;
    const char* fragmentEntry;
    const char* computeEntry;
    GpuShaderProgram program;
    GpuPipelineLayout layout;
    GpuPipelineHandle pipeline;
    uint64_t pipelineLayoutHash;
} GpuDdrPassDesc;

typedef struct {
    const char* name;
    uint32_t passCount;
    const GpuDdrPassDesc* passes;
} GpuDdrTechniqueDesc;
```

建议第一阶段支持固定几类 technique：`depth_prepass`、`shadow_caster`、`forward_lit`、`visibility_buffer`、`material_resolve`、`compute_cull`、`postprocess`。基础跑通后，再引入自定义 technique parser。

### 9.5 与 GpuGraph 的集成方式

DDR 不应自己 begin/end render pass。推荐 DDR pass 编译成 `GpuGraphPass`：

```c
GpuResult gpuDdrBuildGraph(
    GpuDdrContext ctx,
    const GpuDdrFrameDesc* frame,
    GpuGraph graph);
```

内部流程：

1. DDR 根据 frame desc 创建或 import graph resource，例如 backbuffer、depth、visibility buffer、light list。
2. DDR 根据 technique 创建 graph pass。
3. 每个 pass 声明 `gpuGraphPassRead/Write/ReadWrite`。
4. render pass 设置 color/depth attachment。
5. callback 中执行 pipeline bind、bindless heap bind、draw packet submit 或 dispatch。

例 24 里的手写 graph 构建可以迁移成 DDR：

```text
当前:
main.cpp 手写 shadow/light_cull/ssgi/forward pass

目标:
GpuDdrTechnique sponzaTechnique
GpuDdrScene scene
GpuDdrFrameDesc frame
gpuDdrBuildGraph(ctx, &frame, graph)
gpuGraphCompile(graph)
gpuGraphExecute(graph, queue)
```

### 9.6 Shader 宏与 Slang 侧约定

DDR shader 不建议依赖 DX12-only `ResourceDescriptorHeap` 宏作为唯一表达。仓库基于 slang-rhi，应优先使用 Slang 可跨后端的 descriptor handle / bindless capability，同时保留宏层做语法统一。

建议建立统一 include：

```text
src/gpu/ddr/shaders/ddr_bindless.slang
src/gpu/ddr/shaders/ddr_draw.slang
src/gpu/ddr/shaders/ddr_material.slang
```

核心约定：

```hlsl
struct DdrTexture2D { uint index; };
struct DdrBuffer    { uint index; };
struct DdrSampler   { uint index; };

struct DdrDrawData {
    uint meshId;
    uint materialId;
    uint instanceId;
    uint drawPacketId;
};

struct DdrMaterialData {
    uint baseColorTexture;
    uint normalTexture;
    uint metallicRoughnessTexture;
    uint sampler;
    float4 baseColorFactor;
    float metallicFactor;
    float roughnessFactor;
    uint flags;
};
```

DDR shader 侧应统一封装：

```hlsl
Texture2D ddrGetTexture2D(uint index);
SamplerState ddrGetSampler(uint index);
DdrMaterialData ddrLoadMaterial(uint materialId);
DdrInstanceData ddrLoadInstance(uint instanceId);
```

### 9.7 GPU Driven Path

DDR 第一版应同时支持 CPU submit fallback 和 GPU driven submit。

CPU fallback 适合早期验证：

```text
for packet in visiblePackets:
  bind pipeline by packet.pipelineKey
  set draw constants/material id
  drawIndexed(packet)
```

GPU driven 推荐流程：

```text
CPU:
  upload instances/materials/drawPackets

Compute pass:
  frustum/occlusion culling
  material/pipeline binning
  compact visible draw packets
  write indirect args

Render pass:
  execute indirect / multi draw indirect
  shader uses drawId/instanceId -> materialId -> bindless texture
```

需要新增 frame buffers：

```c
typedef struct {
    GpuBufferHandle instanceBuffer;
    GpuBufferHandle materialBuffer;
    GpuBufferHandle drawPacketBuffer;
    GpuBufferHandle visibleDrawBuffer;
    GpuBufferHandle indirectArgBuffer;
    GpuBufferHandle drawCountBuffer;
} GpuDdrFrameBuffers;
```

如果当前 RHI 尚未完整支持 execute indirect，第一阶段应：

- D3D12/Vulkan native path 走 indirect。
- 不支持时退回 CPU draw packet submit。
- feature table 增加 `GPU_FEATURE_INDIRECT_DRAW`、`GPU_FEATURE_MULTI_DRAW_INDIRECT`、`GPU_FEATURE_DRAW_ID`。

### 9.8 DDR Public API 草案

```c
typedef struct GpuDdrContext_t* GpuDdrContext;
typedef struct GpuDdrScene_t* GpuDdrScene;
typedef struct GpuDdrTechnique_t* GpuDdrTechnique;

typedef struct {
    GpuDevice device;
    uint32_t maxTextures;
    uint32_t maxBuffers;
    uint32_t maxSamplers;
    uint32_t maxMaterials;
    uint32_t maxMeshes;
    uint32_t maxInstances;
    bool enableGpuDriven;
    bool enableValidation;
} GpuDdrContextDesc;

GpuResult gpuDdrCreateContext(const GpuDdrContextDesc* desc, GpuDdrContext* outCtx);
void gpuDdrDestroyContext(GpuDdrContext ctx);

GpuResult gpuDdrCreateScene(GpuDdrContext ctx, GpuDdrScene* outScene);
void gpuDdrDestroyScene(GpuDdrScene scene);

GpuResult gpuDdrRegisterTexture(GpuDdrContext ctx, GpuTextureHandle textureView, uint32_t access, GpuDdrResourceId* outId);
GpuResult gpuDdrRegisterBuffer(GpuDdrContext ctx, GpuBufferHandle buffer, uint32_t access, GpuDdrResourceId* outId);
GpuResult gpuDdrRegisterSampler(GpuDdrContext ctx, GpuSamplerHandle sampler, GpuDdrResourceId* outId);

GpuResult gpuDdrCreateMaterialSchema(GpuDdrContext ctx, const GpuDdrMaterialSchemaDesc* desc, uint32_t* outSchemaId);
GpuResult gpuDdrCreateMaterial(GpuDdrContext ctx, uint32_t schemaId, GpuDdrMaterialId* outMaterial);
GpuResult gpuDdrMaterialSetTexture(GpuDdrContext ctx, GpuDdrMaterialId material, const char* name, GpuDdrResourceId texture);
GpuResult gpuDdrMaterialSetFloat4(GpuDdrContext ctx, GpuDdrMaterialId material, const char* name, const float* value);

GpuResult gpuDdrCreateMesh(GpuDdrScene scene, const GpuDdrMeshDesc* desc, GpuDdrMeshId* outMesh);
GpuResult gpuDdrCreateInstance(GpuDdrScene scene, const GpuDdrInstanceDesc* desc, GpuDdrInstanceId* outInstance);

GpuResult gpuDdrCreateTechnique(GpuDdrContext ctx, const GpuDdrTechniqueDesc* desc, GpuDdrTechnique* outTechnique);
void gpuDdrDestroyTechnique(GpuDdrTechnique technique);

typedef struct {
    GpuSurfaceTexture backbuffer;
    uint32_t width;
    uint32_t height;
    float view[16];
    float proj[16];
    float cameraPosition[3];
    uint32_t frameIndex;
} GpuDdrFrameDesc;

GpuResult gpuDdrBuildGraph(GpuDdrContext ctx, GpuDdrScene scene, GpuDdrTechnique technique,
                           const GpuDdrFrameDesc* frame, GpuGraph graph);
```

### 9.9 内部编译流程

DDR technique 编译分为四步：

1. Shader reflection：对每个 pass 调 `gpuReflectPipelineLayout`，提取 binding ranges、bindless space、push constants、entry point。
2. Layout validation：验证 shader 约定字段是否存在，例如 material buffer、instance buffer、draw packet buffer、bindless texture heap。
3. Pipeline build：使用 layout hash、render state、RT format、shader program 生成 pipeline cache key。
4. Runtime binding plan：生成 pass callback 需要执行的 binding list，将 resource id 解析为 bindless index，并写入 per-frame/per-pass constants。

### 9.10 与现有 example 24 的迁移方案

建议不要直接删除例 24，而是新增 `examples/25_ddr_sponza`，分阶段迁移：

1. 资源登记：复用 `SponzaScene` loader，将 material textures、light buffer、depth、SSGI output 登记到 DDR resource table。
2. 材质表：把 `SponzaMaterial` 转成 `GpuDdrMaterialGpu`，上传 material buffer，forward shader 改为 `materialId -> materialBuffer -> bindless texture`。
3. draw packet：把 `scene.draws` 转成 `GpuDdrDrawPacket`，CPU fallback path 使用 draw packet 画完整 Sponza。
4. graph builder：把手写 shadow/light_cull/ssgi/forward pass 换成 `gpuDdrBuildGraph`。
5. GPU culling：新增 compute cull shader，输出 visible draw packet 和 indirect args；无 indirect 能力时仍退回 CPU visible packet submit。

### 9.11 风险点

1. Slang bindless 跨后端差异：DDR shader 必须走统一 include，不允许 demo 自己拼 `.Handle` 访问。
2. draw id 获取：D3D12/Vulkan/Metal 对 draw id、base instance、multi draw indirect 支持不同。第一阶段优先使用 `instanceId/drawPacketId buffer`，不要强依赖某个后端的 draw id builtin。
3. pipeline 切换与材质排序：DDR 需要 pipelineKey/binning。第一阶段 CPU 排序，第二阶段 GPU binning。
4. 资源生命周期：DDR table 必须订阅资源释放或统一托管资源，否则 bindless slot 会悬空。
5. shader permutation：不要一开始做完整 HFX。先用 technique desc + Slang reflection，之后再接 effect language。

### 9.12 实施优先级

P0：最小可用 DDR

- `GpuDdrContext`
- resource table
- material schema/material instance
- mesh/instance/draw packet
- CPU fallback draw packet submit
- 一个 `25_ddr_sponza`，使用 bindless material texture

P1：Render Graph 集成

- `gpuDdrBuildGraph`
- DDR pass -> GpuGraph pass
- forward/shadow/ssgi/light cull technique
- graph export 包含 DDR pass metadata

P2：GPU Driven

- culling compute pass
- visible draw buffer
- indirect arg buffer
- feature-gated indirect draw path
- CPU fallback 保持可用

P3：Shader/Effect 数据化

- technique JSON 或 HFX-like 描述
- reflection 生成 material schema
- pipeline cache 预热
- shader hot reload

P4：Production 化

- validation callback
- resource lifetime/deferred destroy
- descriptor slot leak detection
- render tests/readback image diff
- backend conformance matrix

### 9.13 建议结论

在当前仓库里实现 DDR 的最佳路线是：**不要从 shader 宏开始，不要从 HFX parser 开始，而是从 `GpuDdrContext + ResourceTable + MaterialBuffer + DrawPacket + GpuGraph integration` 开始**。

原因很直接：仓库已经有 bindless、pipeline layout、render graph 和 Sponza graph demo，这些就是 DDR 的地基。先把例 24 里散落的场景数据、材质绑定、pass 构建、draw 调用抽成稳定 API，再逐步加入 GPU culling 和 indirect draw。这样每一步都有可运行 demo 验证，不会变成只存在于文档里的“大框架”。
