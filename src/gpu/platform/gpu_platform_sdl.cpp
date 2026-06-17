#include "gpu/platform/gpu_platform.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>

static bool s_platformInitialized = false;

GpuResult gpuPlatformInit(void)
{
    if (s_platformInitialized) return GPU_SUCCESS;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        return GPU_ERROR_INTERNAL;
    }
    s_platformInitialized = true;
    return GPU_SUCCESS;
}

void gpuPlatformShutdown(void)
{
    if (s_platformInitialized) {
        SDL_Quit();
        s_platformInitialized = false;
    }
}

struct GpuWindow_t {
    SDL_Window* sdlWindow;
    uint32_t width;
    uint32_t height;
};

GpuResult gpuCreateWindow(const GpuWindowDesc* desc, GpuWindow* outWindow)
{
    if (!desc || !outWindow) return GPU_ERROR_INVALID_ARGS;

    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, desc->title ? desc->title : "gpu");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, desc->width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, desc->height);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, desc->resizable);

    if (desc->fullscreen) {
        SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, true);
    }

    SDL_Window* sdlWindow = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);

    if (!sdlWindow) return GPU_ERROR_INTERNAL;

    GpuWindow window = new GpuWindow_t();
    window->sdlWindow = sdlWindow;
    window->width = desc->width;
    window->height = desc->height;

    *outWindow = window;
    return GPU_SUCCESS;
}

void gpuDestroyWindow(GpuWindow window)
{
    if (!window) return;
    if (window->sdlWindow) {
        SDL_DestroyWindow(window->sdlWindow);
    }
    delete window;
}

uint32_t gpuWindowGetWidth(GpuWindow window)
{
    if (!window) return 0;
    int w;
    SDL_GetWindowSize(window->sdlWindow, &w, nullptr);
    window->width = (uint32_t)w;
    return window->width;
}

uint32_t gpuWindowGetHeight(GpuWindow window)
{
    if (!window) return 0;
    int h;
    SDL_GetWindowSize(window->sdlWindow, nullptr, &h);
    window->height = (uint32_t)h;
    return window->height;
}

void* gpuWindowGetHandle(GpuWindow window)
{
    if (!window) return nullptr;
    return window->sdlWindow;
}

static GpuPlatformEventType translateEventType(SDL_EventType type)
{
    switch (type) {
    case SDL_EVENT_QUIT:
        return GPU_PLATFORM_EVENT_QUIT;
    case SDL_EVENT_WINDOW_RESIZED:
        return GPU_PLATFORM_EVENT_RESIZE;
    case SDL_EVENT_KEY_DOWN:
        return GPU_PLATFORM_EVENT_KEY_DOWN;
    case SDL_EVENT_KEY_UP:
        return GPU_PLATFORM_EVENT_KEY_UP;
    case SDL_EVENT_MOUSE_MOTION:
        return GPU_PLATFORM_EVENT_MOUSE_MOVE;
    default:
        return (GpuPlatformEventType)-1;
    }
}

bool gpuPollEvent(GpuPlatformEvent* outEvent)
{
    if (!outEvent) return false;

    SDL_Event sdlEvent;
    while (SDL_PollEvent(&sdlEvent)) {
        GpuPlatformEventType type = translateEventType((SDL_EventType)sdlEvent.type);
        if ((int32_t)type == -1) continue;

        outEvent->type = type;
        switch (type) {
        case GPU_PLATFORM_EVENT_RESIZE:
            outEvent->resize.width = (uint32_t)sdlEvent.window.data1;
            outEvent->resize.height = (uint32_t)sdlEvent.window.data2;
            return true;
        case GPU_PLATFORM_EVENT_KEY_DOWN:
        case GPU_PLATFORM_EVENT_KEY_UP:
            outEvent->key.keycode = (uint32_t)sdlEvent.key.key;
            return true;
        case GPU_PLATFORM_EVENT_MOUSE_MOVE:
            outEvent->mouse.x = (int32_t)sdlEvent.motion.x;
            outEvent->mouse.y = (int32_t)sdlEvent.motion.y;
            return true;
        case GPU_PLATFORM_EVENT_QUIT:
            return true;
        default:
            continue;
        }
    }
    return false;
}
