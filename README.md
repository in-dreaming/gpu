# gpu

A C API GPU abstraction layer built on [Slang](https://github.com/shader-slang/slang) and [slang-rhi](https://github.com/shader-slang/slang-rhi), with SDL3 for cross-platform windowing.

## Design

- **Pure C API** — all public headers are C11 with `extern "C"` guards; C++ is hidden behind PIMPL in `.cpp` implementation files
- **Shader-Centric** — Slang reflection drives resource layout, eliminating hand-maintained binding tables
- **Opaque handles** — `GpuDevice`, `GpuCommandQueue`, `GpuSurface`, etc. are pointer-sized opaque types; internal structs are never exposed
- **Generational handles** — `GpuBufferHandle`, `GpuTextureHandle` use index+generation pairs for safe resource pooling
- **Backend auto-selection** — `gpuCreateDevice` tries Default → Vulkan → D3D12 automatically
- **Cross-platform** — Windows (Vulkan, D3D12), Linux (Vulkan), macOS (Metal), Web (WebGPU)

## Quick Start

### Prerequisites

- CMake 3.24+
- C11 / C++20 compiler (MSVC, GCC, Clang)
- Git (submodules required)

### Build

```bash
git clone --recurse-submodules https://github.com/your-org/gpu.git
cd gpu
cmake -B build
cmake --build build --config Release
```

### Build with Tests

```bash
cmake -B build -DGPU_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

### Run Examples

```bash
# Windows: runtime DLLs are auto-copied next to exe via POST_BUILD
build\examples\Release\00_window.exe
build\examples\Release\00_window_clear.exe
build\examples\Release\01_device_init.exe
```

## API Usage

Single header entry point:

```c
#include "gpu/gpu.h"
```

### Create window + device + surface

```c
gpuPlatformInit();

GpuWindow window;
GpuWindowDesc winDesc = { .title = "app", .width = 800, .height = 600, .vsync = true, .resizable = true };
gpuCreateWindow(&winDesc, &window);

GpuDevice device;
GpuDeviceDesc devDesc = { .appName = "app", .enableDebugLayer = false };
gpuCreateDevice(&devDesc, &device);

GpuSurface surface;
gpuCreateSurface(device, window, GPU_SURFACE_TYPE_VULKAN, &surface);

GpuFormat fmt = gpuSurfaceGetPreferredFormat(surface);
gpuSurfaceConfigure(surface, 800, 600, fmt, true);
```

### Render loop

```c
GpuCommandQueue queue;
gpuGetQueue(device, GPU_QUEUE_TYPE_GRAPHICS, &queue);

GpuPlatformEvent ev;
for (;;) {
    while (gpuPollEvent(&ev)) {
        if (ev.type == GPU_PLATFORM_EVENT_QUIT) goto cleanup;
        if (ev.type == GPU_PLATFORM_EVENT_RESIZE)
            gpuSurfaceConfigure(surface, ev.resize.width, ev.resize.height, fmt, true);
    }

    GpuSurfaceTexture backbuffer = NULL;
    if (gpuSurfaceAcquireNextImage(surface, &backbuffer) != GPU_SUCCESS) continue;

    GpuCommandEncoder enc = gpuBeginCommandEncoder(queue);
    gpuCmdClearSurfaceTexture(enc, backbuffer, 0.1f, 0.1f, 0.2f, 1.0f);

    GpuCommandBuffer cmd = gpuFinishCommandEncoder(enc);
    if (cmd) gpuQueueSubmit(queue, 1, &cmd);

    gpuSurfacePresent(surface);
    gpuSurfaceTextureRelease(backbuffer);
}
```

### Resource creation

```c
GpuBufferHandle buf;
GpuBufferDesc bufDesc = {
    .size = 1024 * 1024,
    .elementSize = 4,
    .usage = GPU_BUFFER_USAGE_SHADER_RESOURCE | GPU_BUFFER_USAGE_UNORDERED_ACCESS,
    .label = "my_buffer",
};
gpuCreateBuffer(device, &bufDesc, &buf);

GpuTextureHandle tex;
GpuTextureDesc texDesc = {
    .type = GPU_TEXTURE_TYPE_2D,
    .width = 256, .height = 256, .depth = 1,
    .arrayLength = 1, .mipCount = 1,
    .format = GPU_FORMAT_RGBA8_UNORM,
    .sampleCount = 1,
    .usage = GPU_TEXTURE_USAGE_SHADER_RESOURCE | GPU_TEXTURE_USAGE_RENDER_TARGET,
    .label = "my_texture",
};
gpuCreateTexture(device, &texDesc, &tex);
```

### Cleanup

```c
cleanup:
gpuQueueWaitOnHost(queue);
gpuSurfaceUnconfigure(surface);
gpuDestroySurface(device, surface);
gpuDestroyBuffer(device, buf);
gpuDestroyTexture(device, tex);
gpuDestroyDevice(device);
gpuDestroyWindow(window);
gpuPlatformShutdown();
```

## Public Headers

| Header | Description |
|:---|:---|
| `gpu/gpu.h` | Umbrella header — includes everything below |
| `gpu/core/gpu_types.h` | `GpuResult` status codes |
| `gpu/core/gpu_handle.h` | `GpuHandle`, typed handle aliases, `GPU_NULL_HANDLE` |
| `gpu/core/gpu_format.h` | `GpuFormat` enum |
| `gpu/core/gpu_device.h` | `GpuDevice` create / destroy |
| `gpu/core/gpu_command.h` | Queue, encoder, command buffer, submit |
| `gpu/core/gpu_render_pass.h` | `gpuCmdClearSurfaceTexture` |
| `gpu/core/gpu_buffer.h` | `GpuBufferHandle` create / destroy |
| `gpu/core/gpu_texture.h` | `GpuTextureHandle` create / destroy |
| `gpu/platform/gpu_platform.h` | SDL3 window + event loop |
| `gpu/platform/gpu_surface.h` | Cross-backend surface create / configure / present |
| `gpu/platform/gpu_swapchain.h` | Convenience swapchain wrapping surface |

## Project Structure

```
src/gpu/
  gpu.h                    # Single entry point
  core/
    gpu_types.h            # C API — result codes
    gpu_handle.h           # C API — generational handle system
    gpu_format.h           # C API — pixel formats
    gpu_device.h/.cpp      # C API + C++ PIMPL
    gpu_command.h          # C API — queue/encoder/buffer
    gpu_render_pass.h/.cpp # C API — clear helpers
    gpu_buffer.h/.cpp      # C API — buffer create/destroy
    gpu_texture.h/.cpp     # C API — texture create/destroy
    gpu_internal.h         # C++ only — PIMPL struct definitions + helpers
    gpu_handle_pool.h      # C++ only — generational handle pool template
  platform/
    gpu_platform.h         # C API — window/events
    gpu_platform_sdl.cpp   # SDL3 implementation
    gpu_surface.h/.cpp     # C API + PIMPL wrapping rhi::ISurface
    gpu_swapchain.h/.cpp   # C API + PIMPL convenience wrapper

examples/
  00_window/               # SDL window + surface creation
  00_window_clear/         # 300-frame clear-to-blue render loop
  01_device_init/          # Device + buffer + texture lifecycle

tests/
  smoke/                   # ctest smoke test (buffer/texture alloc, generational reuse)

modules/3rd/
  sdl/                     # SDL3 (git submodule)
  slang/                   # Slang compiler (git submodule, optional)
  slang-rhi/               # slang-rhi (git submodule)
```

## CMake Options

| Option | Default | Description |
|:---|:---|:---|
| `GPU_ENABLE_VULKAN` | ON | Enable Vulkan backend |
| `GPU_ENABLE_D3D12` | ON | Enable D3D12 backend |
| `GPU_ENABLE_METAL` | OFF | Enable Metal backend |
| `GPU_ENABLE_WEBGPU` | OFF | Enable WebGPU backend |
| `GPU_BUILD_EXAMPLES` | ON | Build example programs |
| `GPU_BUILD_TESTS` | OFF | Build tests |
| `GPU_INSTALL` | ON | Enable install rules |

## Architecture

```
  ┌─────────────────────────────────────┐
  │            C API (gpu.h)            │
  ├──────────┬──────────┬───────────────┤
  │  Platform│  Core    │   Resource    │
  │  (SDL3)  │  (C API) │   (C API)     │
  ├──────────┴──────────┴───────────────┤
  │         PIMPL (C++ internal)        │
  │     Handle Pool │ Surface/Queue     │
  ├─────────────────────────────────────┤
  │           slang-rhi (C++)           │
  ├─────────┬────────┬────────┬────────┤
  │ Vulkan  │ D3D12  │ Metal  │ WGPU   │
  └─────────┴────────┴────────┴────────┘
```

The C API layer keeps the public interface stable and FFI-friendly (Zig, Rust, TS). The C++ PIMPL layer wraps slang-rhi's COM-style interfaces with a generational handle pool for safe resource management. Backend selection is automatic at device creation time.

## License

MIT
