#pragma once

#include <stddef.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void gpuStrncpy(char* dest, size_t destSize, const char* src, size_t count)
{
    if (!dest || destSize == 0) {
        return;
    }
#ifdef _MSC_VER
    strncpy_s(dest, destSize, src ? src : "", count);
#else
    size_t maxCopy = destSize - 1;
    size_t copyCount = count < maxCopy ? count : maxCopy;
    if (src) {
        strncpy(dest, src, copyCount);
    } else {
        copyCount = 0;
    }
    dest[copyCount] = '\0';
#endif
}

static inline char* gpuStrdup(const char* src)
{
    if (!src) {
        return NULL;
    }
#ifdef _MSC_VER
    return _strdup(src);
#else
    return strdup(src);
#endif
}

#ifdef __cplusplus
}
#endif
