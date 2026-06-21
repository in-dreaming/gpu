#pragma once

#include "gpu/core/gpu_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GpuWindow_t* GpuWindow;

typedef struct {
    const char* title;
    uint32_t width;
    uint32_t height;
    bool fullscreen;
    bool resizable;
    bool vsync;
} GpuWindowDesc;

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
    GPU_PLATFORM_EVENT_MOUSE_BUTTON_DOWN = 5,
    GPU_PLATFORM_EVENT_MOUSE_BUTTON_UP = 6,
} GpuPlatformEventType;

typedef struct {
    GpuPlatformEventType type;
    union {
        struct { uint32_t width; uint32_t height; } resize;
        struct { uint32_t keycode; } key;
        struct { int32_t x; int32_t y; int32_t dx; int32_t dy; uint32_t button; } mouse;
    };
} GpuPlatformEvent;

bool gpuPollEvent(GpuPlatformEvent* outEvent);

#ifdef __cplusplus
}
#endif
