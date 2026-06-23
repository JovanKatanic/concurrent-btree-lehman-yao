#pragma once

#include "common.hpp"

#include <memory>
#include <cstring>

namespace db7
{

    template <typename T>
    constexpr T AlignUp(T value, T alignment)
    {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    constexpr byte *AlignUp(byte *value, u32 alignment)
    {
        auto addr = reinterpret_cast<uintptr_t>(value);
        addr = (addr + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
        return reinterpret_cast<byte *>(addr);
    }

    template <typename T>
    constexpr T AlignDown(T value, size_t alignment)
    {
        return value & ~(alignment - 1);
    }

    template <typename T>
    static bool IsAligned(void *ptr)
    {
        return reinterpret_cast<uintptr_t>(ptr) % alignof(T) == 0;
    }
}