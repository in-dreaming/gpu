#include "gpu/platform/gpu_surface.h"
#include "gpu/platform/gpu_platform.h"
#include "gpu/core/gpu_device.h"
#include "gpu/core/gpu_internal.h"

#include <slang-rhi.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

#if SLANG_WINDOWS_FAMILY
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

static rhi::WindowHandle getWindowHandleFromSDL(SDL_Window* sdlWindow)
{
#if SLANG_WINDOWS_FAMILY
    HWND hwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(sdlWindow),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        NULL);
    return rhi::WindowHandle::fromHwnd(hwnd);
#elif SLANG_LINUX_FAMILY
    if (SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11") == 0) {
        void* display = (void*)SDL_GetPointerProperty(
            SDL_GetWindowProperties(sdlWindow),
            SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
            NULL);
        uint32_t xwindow = (uint32_t)SDL_GetNumberProperty(
            SDL_GetWindowProperties(sdlWindow),
            SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
            0);
        return rhi::WindowHandle::fromXlibWindow(display, xwindow);
    }
    return {};
#elif SLANG_APPLE_FAMILY
    void* nswindow = SDL_GetPointerProperty(
        SDL_GetWindowProperties(sdlWindow),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
        NULL);
    return rhi::WindowHandle::fromNSWindow(nswindow);
#else
    return {};
#endif
}

GpuResult gpuCreateSurface(GpuDevice device, GpuWindow window, GpuSurfaceType type, GpuSurface* outSurface)
{
    (void)type;
    if (!device || !window || !outSurface) return GPU_ERROR_INVALID_ARGS;

    rhi::IDevice* rhiDevice = device->rhiDevice;
    SDL_Window* sdlWindow = (SDL_Window*)gpuWindowGetHandle(window);

    rhi::WindowHandle windowHandle = getWindowHandleFromSDL(sdlWindow);
    if (windowHandle.type == rhi::WindowHandleType::Undefined) {
        return GPU_ERROR_NOT_SUPPORTED;
    }

    rhi::ComPtr<rhi::ISurface> rhiSurface;
    if (SLANG_FAILED(rhiDevice->createSurface(windowHandle, rhiSurface.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    GpuSurface surface = new GpuSurface_t();
    surface->rhiSurface = rhiSurface;
    surface->width = gpuWindowGetWidth(window);
    surface->height = gpuWindowGetHeight(window);
    surface->format = GPU_FORMAT_UNDEFINED;
    surface->configured = false;

    *outSurface = surface;
    return GPU_SUCCESS;
}

void gpuDestroySurface(GpuDevice device, GpuSurface surface)
{
    (void)device;
    if (!surface) return;
    if (surface->configured) {
        surface->rhiSurface->unconfigure();
    }
    delete surface;
}

uint32_t gpuSurfaceGetWidth(GpuSurface surface)
{
    return surface ? surface->width : 0;
}

uint32_t gpuSurfaceGetHeight(GpuSurface surface)
{
    return surface ? surface->height : 0;
}

GpuFormat gpuSurfaceGetPreferredFormat(GpuSurface surface)
{
    if (!surface) return GPU_FORMAT_UNDEFINED;
    const rhi::SurfaceInfo& info = surface->rhiSurface->getInfo();
    switch (info.preferredFormat) {
    case rhi::Format::BGRA8Unorm:     return GPU_FORMAT_BGRA8_UNORM;
    case rhi::Format::BGRA8UnormSrgb: return GPU_FORMAT_BGRA8_UNORM_SRGB;
    case rhi::Format::RGBA8Unorm:     return GPU_FORMAT_RGBA8_UNORM;
    case rhi::Format::RGBA8UnormSrgb: return GPU_FORMAT_RGBA8_UNORM_SRGB;
    default:                          return GPU_FORMAT_UNDEFINED;
    }
}

static rhi::Format toRhiFormat(GpuFormat fmt)
{
    switch (fmt) {
    case GPU_FORMAT_BGRA8_UNORM:      return rhi::Format::BGRA8Unorm;
    case GPU_FORMAT_BGRA8_UNORM_SRGB: return rhi::Format::BGRA8UnormSrgb;
    case GPU_FORMAT_RGBA8_UNORM:      return rhi::Format::RGBA8Unorm;
    case GPU_FORMAT_RGBA8_UNORM_SRGB: return rhi::Format::RGBA8UnormSrgb;
    case GPU_FORMAT_R8_UNORM:         return rhi::Format::R8Unorm;
    default:                          return rhi::Format::Undefined;
    }
}

GpuResult gpuSurfaceConfigure(GpuSurface surface, uint32_t width, uint32_t height, GpuFormat format, bool vsync)
{
    if (!surface) return GPU_ERROR_INVALID_ARGS;

    rhi::SurfaceConfig config;
    config.width = width;
    config.height = height;
    config.format = (format == GPU_FORMAT_UNDEFINED) ? rhi::Format::Undefined : toRhiFormat(format);
    config.vsync = vsync;
    config.desiredImageCount = 3;

    if (SLANG_FAILED(surface->rhiSurface->configure(config))) {
        return GPU_ERROR_INTERNAL;
    }

    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->configured = true;
    return GPU_SUCCESS;
}

GpuResult gpuSurfaceUnconfigure(GpuSurface surface)
{
    if (!surface) return GPU_ERROR_INVALID_ARGS;
    if (surface->configured) {
        surface->rhiSurface->unconfigure();
        surface->configured = false;
    }
    return GPU_SUCCESS;
}

GpuResult gpuSurfaceAcquireNextImage(GpuSurface surface, GpuSurfaceTexture* outTexture)
{
    if (!surface || !outTexture) return GPU_ERROR_INVALID_ARGS;
    if (!surface->configured) return GPU_ERROR_INTERNAL;

    rhi::ComPtr<rhi::ITexture> texture;
    if (SLANG_FAILED(surface->rhiSurface->acquireNextImage(texture.writeRef()))) {
        return GPU_ERROR_INTERNAL;
    }

    GpuSurfaceTexture surfTex = new GpuSurfaceTexture_t();
    surfTex->rhiTexture = texture;

    *outTexture = surfTex;
    return GPU_SUCCESS;
}

void gpuSurfaceTextureRelease(GpuSurfaceTexture texture)
{
    if (texture) delete texture;
}

GpuResult gpuSurfacePresent(GpuSurface surface)
{
    if (!surface || !surface->configured) return GPU_ERROR_INVALID_ARGS;
    return SLANG_SUCCEEDED(surface->rhiSurface->present()) ? GPU_SUCCESS : GPU_ERROR_INTERNAL;
}
