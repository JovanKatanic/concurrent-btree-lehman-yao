#pragma once

#include "common.hpp"

#include <concepts>

namespace db7
{
    struct BaseLyHeader
    {
        page_id pid;
        u64 rlink;
        u64 llink;
        u32 count;
        u8 level;
        u64 max_val;
    };

    inline u8 GetLevel(byte *data)
    {
        return reinterpret_cast<BaseLyHeader *>(data)->level;
    }

    inline u32 GetCount(byte *data)
    {
        return reinterpret_cast<BaseLyHeader *>(data)->count;
    }
}