# Phase 0: 平台与构建 (M0)

> L-1 Platform 层实现，建立 SDL 窗口/Surface/Swapchain 基础设施与 CI 构建。

---

## T0.1 L-1 Platform - SDL 窗口与 Surface 管理

### 目标

基于 SDL3 统一跨平台窗口创建、输入事件处理与 GPU Surface 对接，为所有需要屏幕输出的 Example 提供基础。

### 前置依赖

- SDL3 源码或预编译库已集成到 `modules/3rd/SDL`
- CMake 构建系统可用

### 实现细节

#### 0.1.1 平台初始化与窗口创建

**文件**: `src/gpu/platform/gpu_platform.h`

```c
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuPlatform_t* GpuPlatform;

typedef struct {
    const char* title;
    uint32_t width;
    uint32_t height;
    bool fullscreen;
    bool resizable;
    bool vsync;
} GpuWindowDesc;

typedef struct GpuWindow_t* GpuWindow;

GpuResult gpuPlatformInit(void);
void gpuPlatformShutdown(void);

GpuResult gpuCreateWindow(const GpuWindowDesc* desc, GpuWindow* outWindow);
void gpuDestroyWindow(GpuWindow window);

uint32_t gpuWindowGetWidth(GpuWindow window);
uint32_t gpuWindowGetHeight(GpuWindow window);
void* gpuWindowGetHandle(GpuWindow window);

typedef enum {
    GPU_PLATFORM_EVENT_QUIT = 0,
    GPU_PLATFORM_EVENT_RESIZE = 1,
    GPU_PLATFORM_EVENT_KEY_DOWN = 2,
    GPU_PLATFORM_EVENT_KEY_UP = 3,
    GPU_PLATFORM_EVENT_MOUSE_MOVE = 4,
} GpuPlatformEventType;

typedef struct {
    GpuPlatformEventType type;
    union {
        struct { uint32_t width; uint32_t height; } resize;
        struct { uint32_t keycode; } key;
        struct { int32_t x; int32_t y; } mouse;
    };
} GpuPlatformEvent;

bool gpuPollEvent(GpuPlatformEvent* outEvent);

#ifdef __cplusplus
}
#endif
```

**内部实现**: `src/gpu/platform/gpu_platform_sdl.cpp`

- `gpuPlatformInit` → `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)`
- `gpuCreateWindow` → `SDL_CreateWindow` + `SDL_SetWindowResizable`
- `gpuPollEvent` → `SDL_PollEvent` 转换为 `GpuPlatformEvent`
- `gpuWindowGetHandle` → `SDL_GetPointerProperty(SDL_PROP_WINDOW_WIN32_HWND_POINTER)` (Windows) 或对应平台句柄

#### 0.1.2 Surface 创建

**文件**: `src/gpu/platform/gpu_surface.h`

```c
typedef struct GpuSurface_t* GpuSurface;

typedef enum {
    GPU_SURFACE_TYPE_VULKAN = 0,
    GPU_SURFACE_TYPE_D3D12  = 1,
    GPU_SURFACE_TYPE_METAL  = 2,
    GPU_SURFACE_TYPE_WEBGPU = 3,
} GpuSurfaceType;

GpuResult gpuCreateSurface(GpuDevice device, GpuWindow window, GpuSurfaceType type, GpuSurface* outSurface);
void gpuDestroySurface(GpuDevice device, GpuSurface surface);
```

**内部实现**:

| 后端 | Surface 创建方式 |
|:---|:---|
| Vulkan | `SDL_Vulkan_CreateSurface(window, vkInstance, &vkSurface)` |
| D3D12 | 直接使用 `SDL_GetPointerProperty(SDL_PROP_WINDOW_WIN32_HWND_POINTER)` |
| Metal | `SDL_Metal_CreateView(window)` → `SDL_Metal_GetLayer()` |
| WebGPU | `SDL_GetPointerProperty(SDL_PROP_WINDOW_WGPU_SURFACE_POINTER)` |

**验证**:
- 各后端 Surface 创建成功，不触发 Validation Error
- 窗口 resize 时 Surface 重建正确

### Example 测试

**文件**: `examples/00_window/main.c`

```c
#include "gpu/platform/gpu_platform.h"
#include "gpu/platform/gpu_surface.h"
#include "gpu/core/gpu_device.h"
#include <stdio.h>

int main() {
    gpuPlatformInit();

    GpuWindow window;
    GpuWindowDesc winDesc = {
        .title = "00_window",
        .width = 800,
        .height = 600,
        .resizable = true,
        .vsync = true,
    };
    gpuCreateWindow(&winDesc, &window);

    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "00_window", .enableDebugLayer = true };
    gpuCreateDevice(&devDesc, &device);

    GpuSurface surface;
    gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);

    printf("Window %ux%u created with Vulkan surface\n",
           gpuWindowGetWidth(window), gpuWindowGetHeight(window));

    GpuPlatformEvent ev;
    while (true) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) goto done;
            if (ev.type == GPU_PLATFORM_EVENT_RESIZE)
                printf("Resized: %ux%u\n", ev.resize.width, ev.resize.height);
        }
    }

done:
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();
    return 0;
}
```

### 验证流程

1. 窗口正确显示 800x600 标题栏 "00_window"
2. 拖拽缩放窗口，控制台输出新尺寸
3. 关闭窗口正常退出，无内存泄漏
4. Vulkan Surface 创建无 Validation Error

---

## T0.2 Swapchain 管理

### 目标

实现 `GpuSwapchain` 抽象，封装各后端交换链创建、图像获取与呈现逻辑。

### 前置依赖

- T0.1 Platform + Surface
- T1.1 GpuDevice (基础设备创建即可)

### 实现细节

**文件**: `src/gpu/platform/gpu_swapchain.h`

```c
typedef struct GpuSwapchain_t* GpuSwapchain;

typedef struct {
    GpuSurface surface;
    uint32_t width;
    uint32_t height;
    uint32_t format;
    uint32_t imageCount;
    bool vsync;
} GpuSwapchainDesc;

GpuResult gpuCreateSwapchain(GpuDevice device, const GpuSwapchainDesc* desc, GpuSwapchain* outSwapchain);
void gpuDestroySwapchain(GpuDevice device, GpuSwapchain swapchain);

GpuResult gpuSwapchainAcquireNextImage(GpuSwapchain swapchain, GpuTextureHandle* outImage);
GpuResult gpuSwapchainPresent(GpuSwapchain swapchain, uint32_t imageIndex);

uint32_t gpuSwapchainGetImageCount(GpuSwapchain swapchain);
uint32_t gpuSwapchainGetWidth(GpuSwapchain swapchain);
uint32_t gpuSwapchainGetHeight(GpuSwapchain swapchain);
```

**内部实现**:

| 后端 | 实现方式 |
|:---|:---|
| Vulkan | `VkSwapchainKHR` + `vkAcquireNextImageKHR` + `vkQueuePresentKHR` |
| D3D12 | `IDXGISwapChain3` + `GetCurrentBackBufferIndex()` + `Present()` |
| Metal | `CAMetalLayer` + `nextDrawable` + `presentDrawable:` |
| WebGPU | `wgpuSurfaceConfigure` + `wgpuSurfaceGetCurrentTexture` + `wgpuSurfacePresent` |

**Sync 语义**:
- `AcquireNextImage` 内部使用 semaphore (Vulkan) / fence (D3D12) 确保图像可用
- `Present` 等待渲染完成信号后再提交

### Example 测试

**文件**: `examples/00_window_clear/main.c`

```c
#include "gpu/platform/gpu_platform.h"
#include "gpu/platform/gpu_surface.h"
#include "gpu/platform/gpu_swapchain.h"
#include "gpu/core/gpu_device.h"
#include <stdio.h>

int main() {
    gpuPlatformInit();

    GpuWindow window;
    GpuWindowDesc winDesc = { .title = "00_window_clear", .width = 800, .height = 600, .vsync = true };
    gpuCreateWindow(&winDesc, &window);

    GpuDevice device;
    GpuDeviceDesc devDesc = { .appName = "00_window_clear", .enableDebugLayer = true };
    gpuCreateDevice(&devDesc, &device);

    GpuSurface surface;
    gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);

    GpuSwapchain swapchain;
    GpuSwapchainDesc scDesc = {
        .surface = surface,
        .width = 800,
        .height = 600,
        .format = GPU_FORMAT_BGRA8,
        .imageCount = 3,
        .vsync = true,
    };
    gpuCreateSwapchain(device, &scDesc, &swapchain);

    uint32_t frameCount = 0;
    GpuPlatformEvent ev;
    while (frameCount < 300) {
        while (gpuPollEvent(&ev)) {
            if (ev.type == GPU_PLATFORM_EVENT_QUIT) goto done;
        }

        GpuTextureHandle backbuffer;
        gpuSwapchainAcquireNextImage(swapchain, &backbuffer);

        GpuCommandBuffer cmd = gpuBeginCommandBuffer(device);
        float clearColor[4] = { 0.1f, 0.1f, 0.2f, 1.0f };
        gpuCmdClearColor(cmd, backbuffer, clearColor);
        gpuEndCommandBuffer(cmd);

        GpuCommandQueue queue;
        gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);
        gpuQueueSubmit(queue, 1, &cmd);

        gpuSwapchainPresent(swapchain, 0);
        frameCount++;
    }

done:
    printf("Rendered %u frames\n", frameCount);
    gpuDestroySwapchain(device, swapchain);
    gpuDestroySurface(device, surface);
    gpuDestroyDevice(device);
    gpuDestroyWindow(window);
    gpuPlatformShutdown();
    return 0;
}
```

### 验证流程

1. 窗口呈现深蓝色清屏画面
2. 连续 300 帧不崩溃、不闪烁
3. VSync 开启时帧率锁定显示器刷新率
4. 窗口 resize 后 Swapchain 重建，画面继续正常
5. Vulkan Validation Layer 无错误

---

## T0.3 构建系统与 CI

### 目标

建立 CMake 构建体系与基础 CI 流水线，确保项目可在 Windows/Linux/macOS 上编译运行。

### 前置依赖

- 项目目录结构已确定

### 实现细节

#### 0.3.1 CMake 构建体系

**文件**: `CMakeLists.txt` (顶层)

```cmake
cmake_minimum_required(VERSION 3.24)
project(gpu LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

option(GPU_ENABLE_VULKAN "Enable Vulkan backend" ON)
option(GPU_ENABLE_D3D12 "Enable D3D12 backend" ON)
option(GPU_ENABLE_METAL "Enable Metal backend" OFF)
option(GPU_ENABLE_WEBGPU "Enable WebGPU backend" OFF)
option(GPU_BUILD_EXAMPLES "Build examples" ON)
option(GPU_BUILD_TESTS "Build tests" ON)

add_subdirectory(modules/3rd/slang-rhi)
add_subdirectory(src/gpu)

if(GPU_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()
```

**依赖集成**:
- `modules/3rd/SDL` — SDL3 (via `FetchContent` 或子目录)
- `modules/3rd/slang-rhi` — 已有
- Slang SDK — 通过 slang-rhi 的 `find_package` 传递

#### 0.3.2 CI 流水线

**文件**: `.github/workflows/ci.yml`

```yaml
name: CI
on: [push, pull_request]
jobs:
  build:
    strategy:
      matrix:
        os: [windows-latest, ubuntu-latest, macos-latest]
        config: [Debug, Release]
    runs-on: ${{ matrix.os }}
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive
      - uses: actions/setup-python@v5
      - name: Configure
        run: cmake -B build -DCMAKE_BUILD_TYPE=${{ matrix.config }}
      - name: Build
        run: cmake --build build --config ${{ matrix.config }}
      - name: Test
        run: ctest --test-dir build -C ${{ matrix.config }} --output-on-failure
```

### 验证流程

1. Windows + MSVC: `cmake -B build && cmake --build build` 成功
2. Linux + GCC/Clang: 同上
3. macOS + Clang: 同上 (Metal 后端可选)
4. CI 绿灯通过
5. Example 可独立编译运行

---

## Phase 0 集成验收

| 验收项 | 标准方法 |
|:---|:---|
| SDL 初始化 | `00_window` 创建窗口并响应 resize 事件 |
| Surface 创建 | Vulkan/D3D12 Surface 创建无 Validation Error |
| Swapchain 呈现 | `00_window_clear` 连续渲染 300 帧蓝色画面无闪烁 |
| 跨平台构建 | Windows/Linux/macOS CMake 编译通过 |
| CI 自动化 | Push/PR 触发 CI 全绿 |
