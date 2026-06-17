# gpu

A C API GPU abstraction layer built on [Slang](https://github.com/shader-slang/slang) and [slang-rhi](https://github.com/shader-slang/slang-rhi), with SDL3 for cross-platform windowing.

## Design

- **Pure C API** — all public headers are C11 with `extern "C"` guards; C++ is hidden behind PIMPL in `.cpp` implementation files
- **Shader-Centric** — Slang reflection drives resource layout, eliminating hand-maintained binding tables
- **Opaque handles** — `GpuDevice`, `GpuCommandQueue`, `GpuSurface`, etc. are pointer-sized opaque types; internal structs are never exposed
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
cmake --build build --config Debug
```

### Run Examples

```bash
# Windows: copy runtime DLLs next to exe, then:
build\examples\Debug\00_window.exe
build\examples\Debug\00_window_clear.exe
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

### Cleanup

```c
cleanup:
gpuQueueWaitOnHost(queue);
gpuSurfaceUnconfigure(surface);
gpuDestroySurface(device, surface);
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
| `gpu/core/gpu_command.h` | Queue, encoder, command buffer, submit, fence |
| `gpu/core/gpu_render_pass.h` | `gpuCmdClearSurfaceTexture` |
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
    gpu_internal.h         # C++ only - PIMPL struct definitions
  platform/
    gpu_platform.h         # C API — window/events
    gpu_platform_sdl.cpp   # SDL3 implementation
    gpu_surface.h/.cpp     # C API + PIMPL wrapping rhi::ISurface
    gpu_swapchain.h/.cpp   # C API + PIMPL convenience wrapper

examples/
  00_window/               # SDL window + surface creation
  00_window_clear/         # 300-frame clear-to-blue render loop
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

## Architecture

```
  ┌─────────────────────────────────────┐
  │            C API (gpu.h)            │
  ├──────────┬──────────┬───────────────┤
  │  Platform│  Core    │   Resource    │
  │  (SDL3)  │  (C API) │   (C API)     │
  ├──────────┴──────────┴───────────────┤
  │         PIMPL (C++ internal)        │
  ├─────────────────────────────────────┤
  │           slang-rhi (C++)           │
  ├─────────┬────────┬────────┬────────┤
  │ Vulkan  │ D3D12  │ Metal  │ WGPU   │
  └─────────┴────────┴────────┴────────┘
```

The C API layer keeps the public interface stable and FFI-friendly (Zig, Rust, TS). The C++ PIMPL layer wraps slang-rhi's COM-style interfaces. Backend selection is automatic at device creation time.

## License

MIT
