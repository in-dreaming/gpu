#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t GpuResult;

#define GPU_OK                   0
#define GPU_SUCCESS              0
#define GPU_ERROR_INVALID_ARGS  -1
#define GPU_ERROR_INVALID_PARAMETER -1
#define GPU_ERROR_OUT_OF_MEMORY -2
#define GPU_ERROR_NOT_SUPPORTED -3
#define GPU_ERROR_DEVICE_LOST   -4
#define GPU_ERROR_INTERNAL      -5
#define GPU_ERROR_UNKNOWN       -6
#define GPU_ERROR_NOT_FOUND     -7
#define GPU_ERROR_TIMEOUT       -8
#define GPU_ERROR_BUFFER_TOO_SMALL -9

#ifdef __cplusplus
}
#endif
