#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>

template<typename T, uint32_t PoolSize = 4096>
struct GpuHandlePool {
    static_assert(PoolSize > 1, "PoolSize must be > 1 (slot 0 is reserved)");

    struct Slot {
        T* ptr;
        uint32_t generation;
        bool alive;
    };

    Slot slots[PoolSize] = {};
    uint32_t* freeList = nullptr;
    uint32_t freeCount = 0;
    std::mutex mutex;

    GpuHandlePool()
    {
        freeList = (uint32_t*)malloc(sizeof(uint32_t) * (PoolSize - 1));
        memset(slots, 0, sizeof(slots));
        for (uint32_t i = 1; i < PoolSize; i++) {
            freeList[i - 1] = PoolSize - i;
        }
        freeCount = PoolSize - 1;
    }

    ~GpuHandlePool()
    {
        free(freeList);
    }

    GpuHandlePool(const GpuHandlePool&) = delete;
    GpuHandlePool& operator=(const GpuHandlePool&) = delete;

    uint32_t allocate(T* ptr)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (freeCount == 0) return 0;
        uint32_t idx = freeList[--freeCount];
        Slot& s = slots[idx];
        s.ptr = ptr;
        s.alive = true;
        if (s.generation == 0) s.generation = 1;
        return idx;
    }

    T* resolve(uint32_t index, uint32_t generation) const
    {
        if (index == 0 || index >= PoolSize) return nullptr;
        const Slot& s = slots[index];
        if (!s.alive || s.generation != generation) return nullptr;
        return s.ptr;
    }

    void release(uint32_t index, uint32_t generation)
    {
        if (index == 0 || index >= PoolSize) return;
        std::lock_guard<std::mutex> lock(mutex);
        Slot& s = slots[index];
        if (!s.alive || s.generation != generation) return;
        s.alive = false;
        s.ptr = nullptr;
        s.generation++;
        if (s.generation == 0) s.generation = 1;
        freeList[freeCount++] = index;
    }
};
