#pragma once

#include "common.hpp"

#include <concepts>

namespace db7
{
    template <typename Typ>
    struct BaseLyHeader
    {
        Typ pid;
        Typ rlink;
        Typ llink;
        u32 count;
        u8 level;
        u64 max_val;

        static u8 GetLevel(byte *data)
        {
            return reinterpret_cast<BaseLyHeader<Typ> *>(data)->level;
        }

        static u32 GetCount(byte *data)
        {
            return reinterpret_cast<BaseLyHeader<Typ> *>(data)->count;
        }
    };

    template <typename Typ>
    struct ResultObj
    {
        Typ value;

        bool success;

        ResultObj() = delete;

        ResultObj(bool success) : success(success) {}

        ResultObj(Typ value, bool success) : value(value), success(success) {}
    };
}