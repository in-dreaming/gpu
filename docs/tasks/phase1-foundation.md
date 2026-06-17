# Phase 1: 核心基础设施 (M0-M3)

> L0-L4 层级实现，建立 Shader-Centric 架构底座。

---

## T1.1 L0 Runtime - 设备与资源 C API 封装

### 目标

封装 Slang-RHI 的 `IDevice`/`IBuffer`/`ITexture`，对外暴露纯 C API，实现 Handle 化资源管理与 PIMPL 隔离。

### 前置依赖

- Slang-RHI 源码已集成到 `modules/3rd/slang-rhi`
- CMake 构建系统已配置

### 实现细节

#### 1.1.1 基础类型与状态码

**文件**: `src/gpu/core/gpu_types.h`

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t GpuResult;
#define GPU_SUCCESS              0
#define GPU_ERROR_INVALID_ARGS  -1
#define GPU_ERROR_OUT_OF_MEMORY -2
#define GPU_ERROR_NOT_SUPPORTED -3
#define GPU_ERROR_DEVICE_LOST   -4

#ifdef __cplusplus
}
#endif
```

**验证**: 编译通过，类型大小正确 (`sizeof(GpuResult) == 4`)。

#### 1.1.2 世代索引句柄系统

**文件**: `src/gpu/core/gpu_handle.h`

```c
typedef struct {
    uint32_t index;
    uint32_t generation;
} GpuHandle;

typedef GpuHandle GpuBufferHandle;
typedef GpuHandle GpuTextureHandle;
typedef GpuHandle GpuShaderObjectHandle;
typedef GpuHandle GpuPipelineHandle;

#define GPU_NULL_HANDLE ((GpuHandle){0, 0})

static inline bool gpuHandleIsValid(GpuHandle h) {
    return h.index != 0 || h.generation != 0;
}
```

**资源池实现**: `src/gpu/core/gpu_handle_pool.h` (模板化的句柄池)

```cpp
template<typename T, uint32_t PoolSize = 4096>
struct GpuHandlePool {
    struct Slot {
        T* ptr;
        uint32_t generation;
        bool alive;
    };
    Slot slots[PoolSize];
    uint32_t freeList[PoolSize];
    uint32_t freeCount;

    GpuHandle allocate(T* ptr);
    T* resolve(GpuHandle handle) const;
    void release(GpuHandle handle);
};
```

**验证**:
- 分配/释放循环 1000 次，generation 单调递增
- 释放后再用旧 handle 解析，返回 nullptr
- 池满时分配返回 `GPU_NULL_HANDLE`

#### 1.1.3 GpuDevice C API

**文件**: `src/gpu/core/gpu_device.h`

```c
typedef struct GpuDevice_t* GpuDevice;

typedef struct {
    const char* appName;
    uint32_t adapterIndex;
    bool enableDebugLayer;
} GpuDeviceDesc;

GpuResult gpuCreateDevice(const GpuDeviceDesc* desc, GpuDevice* outDevice);
void gpuDestroyDevice(GpuDevice device);
```

**文件**: `src/gpu/core/gpu_device.cpp` (PIMPL 实现)

```cpp
struct GpuDevice_t {
    slang_rhi::DevicePtr rhiDevice;
    GpuHandlePool<slang_rhi::IBuffer> bufferPool;
    GpuHandlePool<slang_rhi::ITexture> texturePool;
    GpuHandlePool<slang_rhi::IShaderObject> shaderObjectPool;
    GpuHandlePool<slang_rhi::IPipeline> pipelinePool;
    std::mutex deviceMutex;
};
```

**验证**:
- 创建设备成功，返回 `GPU_SUCCESS`
- `enableDebugLayer=true` 时 Vulkan validation layers 激活
- 重复销毁不崩溃 (双重释放保护)

#### 1.1.4 GpuBuffer / GpuTexture C API

```c
typedef struct {
    size_t size;
    uint32_t usage;
} GpuBufferDesc;

GpuResult gpuCreateBuffer(GpuDevice device, const GpuBufferDesc* desc, GpuBufferHandle* outHandle);
GpuResult gpuDestroyBuffer(GpuDevice device, GpuBufferHandle handle);

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t format;
    uint32_t mipCount;
    uint32_t usage;
} GpuTextureDesc;

GpuResult gpuCreateTexture(GpuDevice device, const GpuTextureDesc* desc, GpuTextureHandle* outHandle);
GpuResult gpuDestroyTexture(GpuDevice device, GpuTextureHandle handle);
```

**验证**:
- 创建 1MB Storage Buffer，handle 有效
- 创建 256x256 RGBA8 纹理，handle 有效
- 创建后立即销毁，无内存泄漏 (ASan 验证)

#### 1.1.5 命令模型 C API

```c
typedef struct GpuCommandQueue_t* GpuCommandQueue;
typedef struct GpuCommandBuffer_t* GpuCommandBuffer;

typedef enum {
    GPU_QUEUE_TYPE_GRAPHICS = 0,
    GPU_QUEUE_TYPE_COMPUTE  = 1,
    GPU_QUEUE_TYPE_TRANSFER = 2,
} GpuQueueType;

GpuResult gpuGetQueue(GpuDevice device, GpuQueueType type, GpuCommandQueue* outQueue);
GpuCommandBuffer gpuBeginCommandBuffer(GpuDevice device);
void gpuEndCommandBuffer(GpuCommandBuffer cmd);
GpuResult gpuQueueSubmit(GpuCommandQueue queue, uint32_t count, GpuCommandBuffer* cmds);
```

**验证**:
- 获取 Graphics 队列成功
- 录制空命令缓冲并提交，无崩溃
- 多线程各自录制 CommandBuffer，统一提交成功

### Example 测试

**文件**: `examples/01_device_init/main.c`

```c
#include "gpu/core/gpu_device.h"
#include <stdio.h>

int main() {
    GpuDevice device;
    GpuDeviceDesc desc = { .appName = "01_device_init", .enableDebugLayer = true };
    GpuResult res = gpuCreateDevice(&desc, &device);
    if (res != GPU_SUCCESS) {
        printf("Failed to create device: %d\n", res);
        return 1;
    }
    printf("Device created successfully\n");

    GpuBufferDesc bufDesc = { .size = 1024 * 1024, .usage = GPU_BUFFER_USAGE_STORAGE };
    GpuBufferHandle buf;
    res = gpuCreateBuffer(device, &bufDesc, &buf);
    printf("Buffer created: index=%u gen=%u result=%d\n", buf.index, buf.generation, res);

    gpuDestroyBuffer(device, buf);
    gpuDestroyDevice(device);
    printf("Cleanup done\n");
    return 0;
}
```

### 验证流程

1. 编译通过 (MSVC + GCC/Clang)
2. 运行 `01_device_init`，stdout 输出:
   ```
   Device created successfully
   Buffer created: index=1 gen=1 result=0
   Cleanup done
   ```
3. 开启 Vulkan Validation Layer 无错误
4. ASan 无内存泄漏报告

---

## T1.2 L1 Shader System - Slang 编译管线

### 目标

封装 Slang 编译器，实现 .slang 源码到多后端着色器二进制的自动化编译，支持模块化编译与缓存。

### 前置依赖

- T1.1 GpuDevice C API
- Slang SDK 集成

### 实现细节

#### 1.2.1 ShaderCompiler C API

**文件**: `src/gpu/shader/gpu_shader_compiler.h`

```c
typedef struct GpuShaderCompiler_t* GpuShaderCompiler;

typedef enum {
    GPU_SHADER_TARGET_SPIRV  = 0,
    GPU_SHADER_TARGET_DXIL   = 1,
    GPU_SHADER_TARGET_MSL    = 2,
    GPU_SHADER_TARGET_WGSL   = 3,
    GPU_SHADER_TARGET_CUDA   = 4,
} GpuShaderTarget;

typedef struct {
    const char* sourcePath;
    const char* entryPoint;
    GpuShaderTarget target;
    const char* const* defineNames;
    const char* const* defineValues;
    uint32_t defineCount;
} GpuShaderCompileDesc;

typedef struct {
    const uint8_t* data;
    size_t size;
} GpuShaderBinary;

GpuResult gpuCreateShaderCompiler(GpuDevice device, GpuShaderCompiler* outCompiler);
GpuResult gpuCompileShader(GpuShaderCompiler compiler, const GpuShaderCompileDesc* desc, GpuShaderBinary* outBinary);
void gpuDestroyShaderCompiler(GpuShaderCompiler compiler);
```

#### 1.2.2 SlangModule 系统

**文件**: `src/gpu/shader/gpu_slang_module.h`

```c
typedef struct GpuSlangModule_t* GpuSlangModule;

GpuResult gpuLoadSlangModule(GpuShaderCompiler compiler, const char* modulePath, GpuSlangModule* outModule);
const char* gpuGetModuleEntryPoint(GpuSlangModule module, uint32_t index);
uint32_t gpuGetModuleEntryPointCount(GpuSlangModule module);
void gpuDestroySlangModule(GpuSlangModule module);
```

#### 1.2.3 Shader 缓存

**文件**: `src/gpu/shader/gpu_shader_cache.h`

基于 SHA-1 的编译结果缓存，避免重复编译。

```c
GpuResult gpuShaderCacheOpen(const char* cacheDir, GpuShaderCache* outCache);
GpuResult gpuShaderCacheLookup(GpuShaderCache cache, const char* key, GpuShaderBinary* outBinary);
GpuResult gpuShaderCacheStore(GpuShaderCache cache, const char* key, const GpuShaderBinary* binary);
void gpuShaderCacheClose(GpuShaderCache cache);
```

### Example 测试

**文件**: `examples/02_shader_compile/main.c`
**Shader**: `examples/02_shader_compile/triangle.slang`

```slang
// triangle.slang
struct VSOutput {
    float4 position : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
VSOutput vertexMain(uint vertexID : SV_VertexID) {
    // ... 简单三角形
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target {
    return float4(input.color, 1.0);
}
```

```c
int main() {
    // 创建设备与编译器
    GpuShaderCompiler compiler;
    gpuCreateShaderCompiler(device, &compiler);

    // 编译 Vertex Shader
    GpuShaderCompileDesc vsDesc = {
        .sourcePath = "triangle.slang",
        .entryPoint = "vertexMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderBinary vsBinary;
    gpuCompileShader(compiler, &vsDesc, &vsBinary);
    printf("VS compiled: %zu bytes\n", vsBinary.size);

    // 编译 Fragment Shader
    GpuShaderCompileDesc fsDesc = {
        .sourcePath = "triangle.slang",
        .entryPoint = "fragmentMain",
        .target = GPU_SHADER_TARGET_SPIRV,
    };
    GpuShaderBinary fsBinary;
    gpuCompileShader(compiler, &fsDesc, &fsBinary);
    printf("FS compiled: %zu bytes\n", fsBinary.size);
    return 0;
}
```

### 验证流程

1. 给定 .slang 文件，成功编译出 SPIR-V / DXIL 二进制
2. 编译失败时返回非零 `GpuResult`，错误信息可获取
3. 二次编译相同源码命中缓存，编译时间缩减 >80%
4. 模块系统: import 其他 .slang 模块编译成功

---

## T1.3 L2 Reflection - 类型反射系统

### 目标

深度集成 Slang 反射系统，将 `TypeLayout` 转化为引擎内部 `GpuTypeInfo`，消灭手动 Binding Layout 维护。

### 前置依赖

- T1.2 ShaderCompiler (需要编译后的反射信息)

### 实现细节

#### 1.3.1 GpuTypeInfo 核心结构

**文件**: `src/gpu/reflection/gpu_type_info.h`

```c
typedef enum {
    GPU_TYPE_KIND_SCALAR,
    GPU_TYPE_KIND_VECTOR,
    GPU_TYPE_KIND_MATRIX,
    GPU_TYPE_KIND_STRUCT,
    GPU_TYPE_KIND_ARRAY,
    GPU_TYPE_KIND_TEXTURE,
    GPU_TYPE_KIND_SAMPLER,
    GPU_TYPE_KIND_BUFFER,
    GPU_TYPE_KIND_PARAMETER_BLOCK,
} GpuTypeKind;

typedef struct GpuTypeInfo GpuTypeInfo;

struct GpuTypeInfo {
    GpuTypeKind kind;
    const char* name;
    uint32_t size;
    uint32_t alignment;
    uint32_t bindingSlot;
    uint32_t bindingSpace;

    union {
        struct { uint32_t count; GpuTypeInfo* element; } array;
        struct { uint32_t fieldCount; struct { const char* name; uint32_t offset; GpuTypeInfo* type; }* fields; } structInfo;
        struct { uint32_t rowCount; uint32_t colCount; GpuTypeInfo* scalarType; } matrix;
        struct { uint32_t count; GpuTypeInfo* scalarType; } vector;
    };
};
```

#### 1.3.2 ReflectionCache

**文件**: `src/gpu/reflection/gpu_reflection_cache.h`

```c
GpuResult gpuReflectionCacheCreate(GpuReflectionCache* outCache);
GpuResult gpuReflectShader(GpuReflectionCache cache, GpuShaderCompiler compiler, const char* modulePath, const char* entryPoint, GpuTypeInfo** outTypeInfo);
void gpuReflectionCacheDestroy(GpuReflectionCache cache);
```

内部实现: 从 Slang 的 `slang::ShaderReflection` 提取参数布局，递归构建 `GpuTypeInfo` 树结构。

#### 1.3.3 TypeScript/JSON 元数据生成

**文件**: `src/gpu/reflection/gpu_meta_gen.h`

```c
GpuResult gpuGenerateTypeScript(GpuTypeInfo* typeInfo, const char* outputPath);
GpuResult gpuGenerateJSON(GpuTypeInfo* typeInfo, const char* outputPath);
```

### Example 测试

**文件**: `examples/03_reflection/main.c`
**Shader**: `examples/03_reflection/material.slang`

```slang
// material.slang
struct MaterialParams {
    float3 baseColor;
    float roughness;
    float metallic;
    int baseColorTex;
    int normalTex;
};

ParameterBlock<MaterialParams> gMaterial;
```

```c
int main() {
    GpuReflectionCache cache;
    gpuReflectionCacheCreate(&cache);

    GpuTypeInfo* typeInfo;
    gpuReflectShader(cache, compiler, "material.slang", "fragmentMain", &typeInfo);

    printf("Type: %s, Kind: %d, Size: %u\n", typeInfo->name, typeInfo->kind, typeInfo->size);
    if (typeInfo->kind == GPU_TYPE_KIND_STRUCT) {
        for (uint32_t i = 0; i < typeInfo->structInfo.fieldCount; i++) {
            printf("  Field[%u]: %s offset=%u\n", i,
                typeInfo->structInfo.fields[i].name,
                typeInfo->structInfo.fields[i].offset);
        }
    }

    gpuGenerateJSON(typeInfo, "material_meta.json");
    return 0;
}
```

### 验证流程

1. 反射输出与 .slang 源码结构一致: 字段名、偏移量、类型匹配
2. 嵌套 ParameterBlock 正确递归解析
3. 生成的 JSON 可被 `jq .` 正确解析
4. 相同 shader 反射结果缓存命中

---

## T1.4 L3 Resource - Handle 化资源管理与同步原语

### 目标

实现基于世代索引句柄的资源管理系统、基础同步原语 (GpuFence) 与资源状态屏障。

### 前置依赖

- T1.1 GpuDevice + Handle Pool

### 实现细节

#### 1.4.1 资源生命周期管理

**文件**: `src/gpu/resource/gpu_resource_manager.h`

```c
GpuResult gpuResourceManagerCreate(GpuDevice device, GpuResourceManager* outMgr);
GpuResult gpuResourceManagerFlush(GpuResourceManager mgr);
void gpuResourceManagerDestroy(GpuResourceManager mgr);
```

内部: 维护待释放队列 (`releaseQueue`)，配合 Fence 确保 GPU 执行完毕后再释放。

```cpp
struct PendingRelease {
    GpuHandle handle;
    uint64_t fenceValue;
};
```

#### 1.4.2 GpuFence 基础同步

**文件**: `src/gpu/sync/gpu_fence.h`

```c
typedef struct GpuFence_t* GpuFence;

GpuResult gpuCreateFence(GpuDevice device, uint64_t initialValue, GpuFence* outFence);
uint64_t gpuFenceGetCompletedValue(GpuFence fence);
GpuResult gpuFenceWait(GpuFence fence, uint64_t value, uint32_t timeoutMs);
void gpuDestroyFence(GpuDevice device, GpuFence fence);

GpuResult gpuQueueSubmitWithFence(GpuCommandQueue queue,
                                   uint32_t cmdCount,
                                   GpuCommandBuffer* cmds,
                                   GpuFence signalFence,
                                   uint64_t signalValue);
```

内部实现:
- Vulkan: `VkFence` + `vkWaitForFences` + `vkGetFenceStatus`; timeline semaphore 可选
- D3D12: `ID3D12Fence` + `SetEventOnCompletion` + `GetCompletedValue`
- Metal: 基于 `MTLSharedEvent` 或 `MTLFence`

**验证**:
- 提交命令后 Signal Fence，CPU Wait 成功返回
- `GetCompletedValue` 在 GPU 完成后递增

#### 1.4.3 资源状态屏障

**文件**: `src/gpu/resource/gpu_barrier.h`

```c
typedef enum {
    GPU_RESOURCE_STATE_UNDEFINED = 0,
    GPU_RESOURCE_STATE_COMMON,
    GPU_RESOURCE_STATE_RENDER_TARGET,
    GPU_RESOURCE_STATE_DEPTH_WRITE,
    GPU_RESOURCE_STATE_DEPTH_READ,
    GPU_RESOURCE_STATE_SHADER_RESOURCE,
    GPU_RESOURCE_STATE_UNORDERED_ACCESS,
    GPU_RESOURCE_STATE_COPY_DEST,
    GPU_RESOURCE_STATE_COPY_SRC,
    GPU_RESOURCE_STATE_PRESENT,
    GPU_RESOURCE_STATE_STORAGE_BUFFER,
} GpuResourceState;

typedef struct {
    GpuHandle resource;
    GpuResourceState stateBefore;
    GpuResourceState stateAfter;
    uint32_t subresourceRange; // 0xFFFFFFFF = all
} GpuResourceBarrier;

void gpuCmdResourceBarriers(GpuCommandBuffer cmd,
                             uint32_t barrierCount,
                             const GpuResourceBarrier* barriers);
```

内部实现:
- Vulkan: `VkImageMemoryBarrier` / `VkBufferMemoryBarrier` + `vkCmdPipelineBarrier`
- D3D12: `D3D12_RESOURCE_BARRIER` + `ResourceBarrier()`
- Metal: 编码器边界自动处理; 显式调用 `MTLBlitCommandEncoder.synchronizeResource` 仅在需要时

**自动化方向** (Phase 2+ 增强): 基于命令录制上下文推导屏障，Phase 1 先实现显式 API。

#### 1.4.4 数据回读 API

**文件**: `src/gpu/resource/gpu_readback.h`

```c
GpuResult gpuCreateReadbackBuffer(GpuDevice device, size_t size, GpuBufferHandle* outHandle);
GpuResult gpuCmdCopyTextureToBuffer(GpuCommandBuffer cmd,
                                     GpuTextureHandle src,
                                     uint32_t srcMip, uint32_t srcSlice,
                                     GpuBufferHandle dst,
                                     uint64_t dstOffset);
GpuResult gpuMapReadbackBuffer(GpuDevice device, GpuBufferHandle handle, void** outPtr);
void gpuUnmapReadbackBuffer(GpuDevice device, GpuBufferHandle handle);
```

用途: Example 中像素值验证、screenshot 保存。

#### 1.4.5 StreamingManager 基础框架

**文件**: `src/gpu/resource/gpu_streaming_manager.h`

```c
GpuResult gpuStreamingManagerCreate(GpuDevice device, size_t uploadHeapSize, GpuStreamingManager* outMgr);
GpuResult gpuStreamingRequest(GpuStreamingManager mgr, GpuTextureHandle texture, uint32_t mipStart, uint32_t mipCount, const void* data, size_t dataSize);
GpuResult gpuStreamingUpdate(GpuStreamingManager mgr);
void gpuStreamingManagerDestroy(GpuStreamingManager mgr);
```

### Example 测试

**文件**: `examples/04_resource_lifecycle/main.c`

```c
int main() {
    GpuResourceManager mgr;
    gpuResourceManagerCreate(device, &mgr);

    GpuBufferHandle buffers[100];
    for (int i = 0; i < 100; i++) {
        GpuBufferDesc desc = { .size = 256, .usage = GPU_BUFFER_USAGE_STORAGE };
        gpuCreateBuffer(device, &desc, &buffers[i]);
    }
    for (int i = 0; i < 100; i++) {
        gpuDestroyBuffer(device, buffers[i]);
    }

    GpuFence fence;
    gpuCreateFence(device, 0, &fence);

    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
    gpuEndCommandBuffer(cmd);
    GpuCommandQueue queue;
    gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
    gpuQueueSubmitWithFence(queue, 1, &cmd, fence, 1);

    gpuFenceWait(fence, 1, 5000);
    printf("Fence completed: value=%llu\n", (unsigned long long)gpuFenceGetCompletedValue(fence));

    gpuResourceManagerFlush(mgr);
    gpuDestroyFence(device, fence);
    printf("All resources released after GPU fence\n");
    return 0;
}
```

**文件**: `examples/04b_barrier_demo/main.c`

```c
int main() {
    GpuTextureHandle renderTarget = ...;
    GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);

    GpuResourceBarrier barriers[] = {
        { renderTarget, GPU_RESOURCE_STATE_UNDEFINED, GPU_RESOURCE_STATE_RENDER_TARGET, 0xFFFFFFFF },
    };
    gpuCmdResourceBarriers(cmd, 1, barriers);

    float clear[4] = { 1, 0, 0, 1 };
    gpuCmdClearColor(cmd, renderTarget, clear);

    GpuResourceBarrier presentBarrier = {
        renderTarget, GPU_RESOURCE_STATE_RENDER_TARGET, GPU_RESOURCE_STATE_PRESENT, 0xFFFFFFFF,
    };
    gpuCmdResourceBarriers(cmd, 1, &presentBarrier);

    gpuEndCommandBuffer(cmd);
    printf("Barrier demo: UNDEFINED → RT → PRESENT\n");
    return 0;
}
```

### 验证流程

1. 大量创建/销毁资源后 flush，无内存泄漏 (ASan)
2. 释放后旧 handle 解析返回 nullptr
3. Fence Wait 成功，`GetCompletedValue` 递增
4. Barrier 流程: compute 写入 → barrier → graphics 读取，数据一致
5. StreamingManager 上传纹理数据后读取验证一致
6. 并发创建资源无数据竞争 (TSan)
7. Readback buffer 地图/Unmap 正确，可读取像素值

---

## T1.5 L4 Bindless - 全局描述符堆

### 目标

实现独立的 Bindless 基础设施，支持 Texture/Buffer 全局索引访问。

### 前置依赖

- T1.1 GpuDevice
- T1.4 ResourceManager

### 实现细节

#### 1.5.1 GpuBindlessHeap C API

**文件**: `src/gpu/bindless/gpu_bindless_heap.h`

```c
typedef struct GpuBindlessHeap_t* GpuBindlessHeap;

typedef struct {
    uint32_t maxDescriptors;
    uint32_t descriptorType;
} GpuBindlessHeapDesc;

GpuResult gpuCreateBindlessHeap(GpuDevice device, const GpuBindlessHeapDesc* desc, GpuBindlessHeap* outHeap);
void gpuDestroyBindlessHeap(GpuBindlessHeap heap);

uint32_t gpuBindlessAllocate(GpuBindlessHeap heap, GpuHandle resource);
void gpuBindlessFree(GpuBindlessHeap heap, uint32_t index);

uint32_t gpuGetBindlessIndex(GpuDevice device, GpuHandle handle);
```

内部实现:
- Vulkan: 使用 `VkDescriptorPool` + `VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT`
- D3D12: 使用 `D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV` shader-visible heap
- Metal: 使用 Argument Buffers 模拟
- WebGPU: 降级为固定绑定槽位

#### 1.5.2 描述符管理

**文件**: `src/gpu/bindless/gpu_descriptor_alloc.h`

自由列表分配器，支持 O(1) 分配/释放:

```cpp
struct DescriptorAllocator {
    uint32_t* freeSlots;
    uint32_t freeCount;
    uint32_t* generations;
    uint32_t capacity;

    uint32_t allocate();
    void free(uint32_t index);
};
```

### Example 测试

**文件**: `examples/05_bindless_texture/main.c`
**Shader**: `examples/05_bindless_texture/bindless_demo.slang`

```slang
// bindless_demo.slang
Texture2D<float4> gTextures[] : register(t0, space1);
SamplerState gSampler : register(s0);

[shader("fragment")]
float4 fragmentMain(float2 uv : TEXCOORD0, uint texIndex : TEXINDEX) : SV_Target {
    return gTextures[NonUniformResourceIndex(texIndex)].Sample(gSampler, uv);
}
```

```c
int main() {
    GpuBindlessHeap heap;
    GpuBindlessHeapDesc heapDesc = { .maxDescriptors = 1024, .descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE };
    gpuCreateBindlessHeap(device, &heapDesc, &heap);

    GpuTextureHandle textures[4];
    uint32_t indices[4];
    for (int i = 0; i < 4; i++) {
        GpuTextureDesc texDesc = { .width = 256, .height = 256, .format = GPU_FORMAT_RGBA8, .mipCount = 1, .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE };
        gpuCreateTexture(device, &texDesc, &textures[i]);
        indices[i] = gpuGetBindlessIndex(device, textures[i]);
        printf("Texture %d -> bindless index %u\n", i, indices[i]);
    }

    // 渲染使用 bindless 索引
    // ...
    return 0;
}
```

### 验证流程

1. 分配 1024 个描述符，索引从 0 递增
2. 释放中间索引后再分配，复用已释放槽位
3. 绑定到 Shader 并采样，像素颜色正确
4. 超出 maxDescriptors 分配返回错误码
5. 多线程并发分配无数据竞争

---

## T1.6 GpuCapabilities 能力查询系统

### 目标

构建独立的功能特性判断系统，实现硬件能力对齐与 Feature Gating。

### 前置依赖

- T1.1 GpuDevice

### 实现细节

**文件**: `src/gpu/capability/gpu_capabilities.h`

```c
typedef struct {
    bool supportMeshShader;
    bool supportRayTracing;
    bool supportBindless;
    bool supportWorkGraph;
    bool supportCooperativeMatrix;
    bool supportSparseResource;
    uint32_t maxTextureDimension;
    uint32_t maxBindlessDescriptors;
    uint32_t minUniformBufferAlignment;
} GpuCapabilities;

void gpuGetCapabilities(GpuDevice device, GpuCapabilities* outCaps);
bool gpuIsFeatureSupported(GpuDevice device, uint32_t feature);
```

### Example 测试

```c
int main() {
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);
    printf("Mesh Shader: %s\n", caps.supportMeshShader ? "YES" : "NO");
    printf("Bindless: %s\n", caps.supportBindless ? "YES" : "NO");
    printf("Max Texture DIm: %u\n", caps.maxTextureDimension);
    return 0;
}
```

### 验证流程

1. Vulkan 后端: Mesh Shader/Bindless 取决于物理设备特性
2. D3D12 后端: 正确检测 Agility SDK 版本
3. 回退路径: 不支持的特性返回 false，不崩溃

---

## Phase 1 集成测试

### 完整渲染三角形 Example (屏幕呈现)

**文件**: `examples/06_triangle/main.c`

整合 T0.1 ~ T0.3 + T1.1 ~ T1.6 所有模块，完成从 SDL 窗口创建到 Swapchain 呈现的完整流程:

```c
int main() {
    // 0. 初始化平台 (T0.1)
    gpuPlatformInit();
    GpuWindow window;
    GpuWindowDesc winDesc = { .title = "06_triangle", .width = 800, .height = 600, .vsync = true };
    gpuCreateWindow(&winDesc, &window);

    // 1. 创建设备 (T1.1)
    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "06_triangle", .enableDebugLayer = true };
    gpuCreateDevice(&devDesc, &device);

    // 2. 创建 Surface + Swapchain (T0.2)
    GpuSurface surface;
    gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);
    GpuSwapchain swapchain;
    GpuSwapchainDesc scDesc = { .surface = surface, .width = 800, .height = 600,
                                .format = GPU_FORMAT_BGRA8, .imageCount = 3, .vsync = true };
    gpuCreateSwapchain(device, &scDesc, &swapchain);

    // 3. 查询能力 (T1.6)
    GpuCapabilities caps;
    gpuGetCapabilities(device, &caps);

    // 4. 编译着色器 (T1.2)
    GpuShaderCompiler compiler;
    gpuCreateShaderCompiler(device, &compiler);

    GpuShaderCompileDesc vsDesc = { .sourcePath = "triangle.slang", .entryPoint = "vertexMain", .target = GPU_SHADER_TARGET_SPIRV };
    GpuShaderBinary vsBinary;
    gpuCompileShader(compiler, &vsDesc, &vsBinary);

    // 5. 反射 (T1.3)
    GpuReflectionCache reflCache;
    gpuReflectionCacheCreate(&reflCache);
    GpuTypeInfo* typeInfo;
    gpuReflectShader(reflCache, compiler, "triangle.slang", "vertexMain", &typeInfo);

    // 6. 创建 Bindless 堆 (T1.5)
    GpuBindlessHeap bindlessHeap;
    GpuBindlessHeapDesc heapDesc = { .maxDescriptors = 64, .descriptorType = GPU_DESCRIPTOR_TYPE_TEXTURE };
    gpuCreateBindlessHeap(device, &heapDesc, &bindlessHeap);

    // 7. 创建顶点缓冲 (T1.1 + T1.4)
    float vertices[] = { /* ... */ };
    GpuBufferDesc vbDesc = { .size = sizeof(vertices), .usage = GPU_BUFFER_USAGE_VERTEX };
    GpuBufferHandle vb;
    gpuCreateBuffer(device, &vbDesc, &vb);

    // 8. 创建 Fence (T1.4)
    GpuFence frameFence;
    gpuCreateFence(device, 0, &frameFence);

    // 9. 渲染循环
    GpuPlatformEvent ev;
    uint64_t frameIndex = 0;
    while (true) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) goto cleanup;
        }

        // 获取后备缓冲
        GpuTextureHandle backbuffer;
        gpuSwapchainAcquireNextImage(swapchain, &backbuffer);

        GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);

        // Barrier: UNDEFINED → RENDER_TARGET
        GpuResourceBarrier beginBarrier = { backbuffer, GPU_RESOURCE_STATE_UNDEFINED, GPU_RESOURCE_STATE_RENDER_TARGET, 0xFFFFFFFF };
        gpuCmdResourceBarriers(cmd, 1, &beginBarrier);

        gpuCmdBindPipeline(cmd, pipeline);
        gpuCmdBindVertexBuffer(cmd, 0, vb, 0);
        gpuCmdDraw(cmd, 3, 1);

        // Barrier: RENDER_TARGET → PRESENT
        GpuResourceBarrier endBarrier = { backbuffer, GPU_RESOURCE_STATE_RENDER_TARGET, GPU_RESOURCE_STATE_PRESENT, 0xFFFFFFFF };
        gpuCmdResourceBarriers(cmd, 1, &endBarrier);

        gpuEndCommandBuffer(cmd);

        GpuCommandQueue queue;
        gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
        gpuQueueSubmitWithFence(queue, 1, &cmd, frameFence, ++frameIndex);

        gpuSwapchainPresent(swapchain, 0);

        // 简易帧同步 (避免 CPU 跑太快)
        if (frameIndex > 2) gpuFenceWait(frameFence, frameIndex - 2, 1000);
    }

cleanup:
    // 10. 清理
    gpuDestroyFence(device, frameFence);
    gpuDestroyBuffer(device, vb);
    gpuDestroyBindlessHeap(bindlessHeap);
    gpuReflectionCacheDestroy(reflCache);
    gpuDestroyShaderCompiler(compiler);
    gpuDestroySwapchain(device, swapchain);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();

    printf("Triangle rendered!\n");
    return 0;
}
```

### Phase 1 里程碑验收

| 验收项 | 标准方法 |
|:---|:---|
| SDL 窗口 | `00_window` 创建窗口响应 resize |
| Swapchain 呈现 | `00_window_clear` 300 帧蓝色清屏无闪烁 |
| 基础反射测试 | T1.3 反射输出与 .slang 源码结构严格一致 |
| Bindless 纹理采样 | T1.5 shader 通过 bindless 索引采样纹理，像素验证正确 |
| 三角形渲染 | `06_triangle` 在 Vulkan 后端屏幕呈现正确三角形 |
| 世代句柄安全 | 释放后再解析返回 nullptr，不混淆新资源 |
| Fence 同步 | 提交后 Wait 成功，ResourceManager 正确延迟释放 |
| 资源屏障 | compute→graphics 数据传递需 barrier，无 barrier 时数据不一致 |
| 线程安全 | Device 资源创建并发无竞争 |
| 内存安全 | ASan 无泄漏，TSan 无数据竞争 |

---

## Phase 1 补充任务

### T1.7 Debug Layer 与错误报告

### 目标

实现可选的 GPU 调试层与详细错误信息 API，支持开发期快速定位问题。

### 前置依赖

- T1.1 GpuDevice

### 实现细节

**文件**: `src/gpu/debug/gpu_debug.h`

```c
typedef enum {
    GPU_DEBUG_LEVEL_NONE = 0,
    GPU_DEBUG_LEVEL_ERROR = 1,
    GPU_DEBUG_LEVEL_WARNING = 2,
    GPU_DEBUG_LEVEL_INFO = 3,
    GPU_DEBUG_LEVEL_VERBOSE = 4,
} GpuDebugLevel;

typedef void (*GpuDebugCallback)(GpuDebugLevel level, const char* message, void* userData);

void gpuSetDebugCallback(GpuDevice device, GpuDebugCallback callback, void* userData);

const char* gpuGetLastError(GpuDevice device);
GpuDebugLevel gpuGetDebugLevel(GpuDevice device);
void gpuSetDebugLevel(GpuDevice device, GpuDebugLevel level);
```

内部:
- 在设备创建时 `enableDebugLayer=true` 激活 Vulkan Validation Layers / D3D12 Debug Layer
- 所有 API 调用失败时设置 thread-local 最后错误信息
- Debug 回调输出到 stderr 或用户自定义目标

### 验证流程

1. `enableDebugLayer=true` 时 Vulkan Validation Layer 消息通过回调输出
2. 故意传入无效参数，`gpuGetLastError` 返回非空描述
3. `enableDebugLayer=false` 时无额外输出，无性能开销

---

### T1.8 Reflection TypeScript 代码生成

### 目标

从 `GpuTypeInfo` 生成 TypeScript 声明文件，支持编辑器 Inspector 与脚本绑定。

### 前置依赖

- T1.3 Reflection

### 实现细节

扩充 T1.3 的 `gpu_meta_gen.h`:

```c
GpuResult gpuGenerateTypeScript(GpuTypeInfo* typeInfo, const char* outputPath);
```

生成示例:
```typescript
// Auto-generated from material.slang
interface MaterialParams {
    baseColor: [number, number, number];
    roughness: number;
    metallic: number;
    baseColorTex: number;
    normalTex: number;
}
```

### 验证流程

1. 生成的 `.d.ts` 文件通过 `tsc --noEmit` 类型检查
2. 结构字段名、类型与 .slang 源码一致
