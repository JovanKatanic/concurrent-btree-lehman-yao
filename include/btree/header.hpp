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
        const char *message;

        Typ value;

        bool success;

        static_assert(!std::is_same_v<Typ, char *>, "Typ cant be char*");

        ResultObj() : success(true) {};

        ResultObj(const char *message, bool success) : message(message), success(success) {}

        ResultObj(Typ value, bool success) : value(value), success(success) {}
    };

    template <>
    struct ResultObj<void>
    {
        const char *message = nullptr;
        bool success = false;

        static ResultObj Ok() { return {nullptr, true}; }
        static ResultObj Fail(const char *m = nullptr) { return {m, false}; }
    };
}