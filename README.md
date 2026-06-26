# gpu

A **C11 GPU abstraction layer** built on [Slang](https://github.com/shader-slang/slang) and [slang-rhi](https://github.com/shader-slang/slang-rhi), with SDL3 for cross-platform windowing.

**[特性说明文档 (Features)](docs/features.md)** · [RenderGraph 改造计划](docs/rhi_rendergraph_completion_plan.md)

## Highlights

- **Pure C API** — stable, FFI-friendly (`extern "C"`, opaque handles, generational resource IDs)
- **Shader-Centric** — Slang reflection drives pipeline layout and binding
- **RenderGraph** — logical resources, automatic barriers, transient pooling, DOT/JSON export
- **Multi-Queue** — dedicated graphics / compute / transfer on Vulkan, D3D12, and Metal; honest alias reporting on WebGPU / D3D11 / CPU
- **Copy & Compute helpers** — `gpuCmdCopyBuffer`, graph copy passes, `gpuComputeBindingDispatch`
- **Vulkan transient heap** — placed buffer allocation prototype for graph transient resources
- **Bindless & descriptors** — heap slots, descriptor sets, pipeline layout reflection (see `phaseD` tests)

## Quick Start

### Prerequisites

- CMake 3.24+
- C11 / C++20 compiler (MSVC, GCC, Clang)
- Git with submodules initialized

### Build

```bash
git clone --recurse-submodules <repo-url>
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

Key regression suites:

```bash
build/tests/Release/phaseC.exe   # RenderGraph (40 cases)
build/tests/Release/phaseD.exe   # Layout / descriptor / bindless (19 cases)
```

### Run Examples

```bash
# Run from the output directory (shaders + DLLs are copied next to the exe)
build/examples/Release/06_triangle.exe
build/examples/Release/22_triangle_graph.exe
build/examples/Release/25_async_compute_graph.exe
```

On Windows, runtime DLLs (Slang, D3D12, SDL3) are copied automatically via POST_BUILD rules.

## API Usage

Single entry point:

```c
#include "gpu/gpu.h"
```

### Window + device + surface

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

    GpuCommandEncoder enc = gpuBeginCommandEncoder(device, queue);
    gpuCmdClearSurfaceTexture(enc, backbuffer, 0.1f, 0.1f, 0.2f, 1.0f);
    GpuCommandBuffer cmd = gpuFinishCommandEncoder(enc);
    if (cmd) gpuQueueSubmit(queue, 1, &cmd);

    gpuSurfacePresent(surface);
    gpuSurfaceTextureRelease(backbuffer);
}
```

### RenderGraph (multi-pass + barriers)

```c
GpuGraph graph;
gpuGraphCreate(device, &graph);
gpuGraphSetExecuteMode(graph, GPU_GRAPH_EXECUTE_MULTI_QUEUE);

GpuGraphResource tex = gpuGraphCreateTexture(graph, &texDesc, "color");
GpuGraphPass pass = gpuGraphAddRenderPass(graph, "main");
GpuGraphColorAttachment ca = { .resource = tex, .loadOp = GPU_LOAD_OP_CLEAR, .storeOp = GPU_STORE_OP_STORE };
gpuGraphPassSetColorAttachments(pass, 1, &ca);
gpuGraphPassSetCallback(pass, my_callback, userData);

gpuGraphCompile(graph);
gpuGraphExecute(graph, queue);
gpuGraphDestroy(graph);
```

### Queue capabilities

```c
GpuQueueInfo computeInfo;
gpuGetQueueInfo(device, GPU_QUEUE_TYPE_COMPUTE, &computeInfo);
// computeInfo.support: GPU_QUEUE_SUPPORT_DEDICATED or GPU_QUEUE_SUPPORT_ALIAS_GRAPHICS
// computeInfo.reason: human-readable backend explanation

if (gpuDeviceSupportsIndependentQueues(device)) {
    /* MULTI_QUEUE can use real async compute / transfer segments */
}
```

See **[docs/features.md](docs/features.md)** for the full capability matrix, transient heap, copy passes, and test coverage.

## Examples

| Example | Topic |
|---------|--------|
| `00_window`, `00_window_clear` | Window & clear loop |
| `01_device_init` | Device / buffer / texture lifecycle |
| `06_triangle`, `06b_triangle_readback` | Graphics pipeline + readback |
| `07_compute_pipeline` | Compute shader |
| `22_triangle_graph`, `23_readback_graph` | RenderGraph basics |
| `24_sponza_graph` | Sponza scene + frame graph (C++) |
| `25_async_compute_graph` | Async compute + graphics, multi-queue |
| `13_backend_switch` | Runtime backend selection |
| `14_feature_gating` | Capability queries |
| `15`–`21` | Tensor, neural, work graph, ray tracing, Sponza data-driven |

## Public Headers (selected)

| Header | Description |
|--------|-------------|
| `gpu/gpu.h` | Umbrella header |
| `gpu/core/gpu_device.h` | Device create / destroy |
| `gpu/core/gpu_command.h` | Queues, encoders, `gpuGetQueueInfo` |
| `gpu/core/gpu_backend.h` | Backend type / name |
| `gpu/rendergraph/gpu_render_graph.h` | RenderGraph API |
| `gpu/resource/gpu_copy.h` | `gpuCmdCopyBuffer` |
| `gpu/resource/gpu_barrier.h` | Resource state transitions |
| `gpu/resource/gpu_transient_heap.h` | Vulkan placed heap (prototype) |
| `gpu/pipeline/gpu_compute_binding.h` | Compute dispatch helper |
| `gpu/queue/gpu_multi_queue.h` | Fence, cross-queue submit |
| `gpu/bindless/gpu_bindless_heap.h` | Bindless slots |
| `gpu/bindless/gpu_descriptor_set.h` | Descriptor sets |
| `gpu/platform/gpu_platform.h` | SDL3 window / events |
| `gpu/platform/gpu_surface.h` | Swapchain surface |

## Project Structure

```
src/gpu/
  gpu.h                         # Single entry point
  core/                         # Device, queue, buffer, texture, pipeline
  platform/                     # SDL3 window, surface, swapchain
  rendergraph/                  # RenderGraph compile & execute
  resource/                     # Barrier, copy, hazard, transient heap, readback
  pipeline/                     # Pipeline state, compute binding
  queue/                        # Multi-queue sync, timestamps
  bindless/                     # Bindless heap, descriptor sets
  shader/                       # Slang compiler integration
  reflection/                   # Layout reflection & meta-gen
  ai/                           # Tensor / neural (experimental)
  raytracing/                   # RT helpers (experimental)

examples/                       # 00–25 progressive samples
tests/                          # smoke, phase1–phaseE
docs/                           # features.md, design notes, task plans
modules/3rd/                    # sdl, slang, slang-rhi submodules
```

## CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `GPU_ENABLE_VULKAN` | ON | Vulkan backend |
| `GPU_ENABLE_D3D12` | ON | D3D12 backend |
| `GPU_ENABLE_METAL` | OFF | Metal backend (macOS) |
| `GPU_ENABLE_WEBGPU` | OFF | WebGPU backend |
| `GPU_BUILD_EXAMPLES` | ON | Build examples |
| `GPU_BUILD_TESTS` | OFF | Build tests (phaseC, phaseD, …) |
| `GPU_INSTALL` | ON | Install rules |

## Architecture

```
  ┌──────────────────────────────────────────┐
  │              C API (gpu.h)               │
  ├──────────┬───────────┬───────────────────┤
  │ Platform │ Core RHI  │ RenderGraph       │
  │  (SDL3)  │ + Resource│ + Queue sync      │
  ├──────────┴───────────┴───────────────────┤
  │         PIMPL (C++ internal)             │
  │   Handle pools │ Barrier / hazard       │
  ├──────────────────────────────────────────┤
  │           slang-rhi (forked submodule)   │
  ├─────────┬────────┬────────┬──────────────┤
  │ Vulkan  │ D3D12  │ Metal  │ WGPU / …     │
  └─────────┴────────┴────────┴──────────────┘
```

The C API stays stable for engine integration and FFI. The C++ layer wraps slang-rhi with generational handles, graph barrier compilation, and backend capability reporting.

## Documentation

| Document | Content |
|----------|---------|
| [docs/features.md](docs/features.md) | **Current feature reference (中文)** |
| [docs/rhi_rendergraph_completion_plan.md](docs/rhi_rendergraph_completion_plan.md) | RHI / RenderGraph roadmap |
| [docs/production_ready_rhi.md](docs/production_ready_rhi.md) | Production-readiness criteria |
| [docs/api.md](docs/api.md) | API design notes |

## License

MIT
