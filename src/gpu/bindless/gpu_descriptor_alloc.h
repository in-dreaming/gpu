#pragma once

#include <cstdint>
#include <vector>
#include <mutex>

struct DescriptorAllocator {
    std::vector<uint32_t> freeSlots;
    std::vector<uint32_t> generations;
    uint32_t capacity = 0;
    std::mutex mutex;

    bool init(uint32_t cap)
    {
        capacity = cap;
        freeSlots.resize(cap);
        generations.assign(cap, 0u);
        for (uint32_t i = 0; i < cap; i++) {
            freeSlots[i] = cap - 1 - i;
        }
        return true;
    }

    uint32_t allocate()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (freeSlots.empty()) return UINT32_MAX;
        uint32_t idx = freeSlots.back();
        freeSlots.pop_back();
        return idx;
    }

    void free(uint32_t index)
    {
        if (index >= capacity) return;
        std::lock_guard<std::mutex> lock(mutex);
        generations[index]++;
        freeSlots.push_back(index);
    }

    uint32_t getGeneration(uint32_t index) const
    {
        if (index >= capacity) return 0;
        return generations[index];
    }
};
