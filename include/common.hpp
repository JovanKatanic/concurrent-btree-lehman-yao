#pragma once

#include <cassert>
#include <stdexcept>
#include <cstdint>
#include <iostream>

namespace db7
{
#define DB7_PAGE_SIZE (1 << 13)

#define CACHE_LINE_SIZE 64

#define DB7_ASSERT(expr, message) assert((expr) && (message))

#define DB7_UNREACHABLE()                 \
    do                                    \
    {                                     \
        DB7_ASSERT(false, "unreachable"); \
        __builtin_unreachable();          \
    } while (0)

    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;

    using i8 = int8_t;
    using i16 = int16_t;
    using i32 = int32_t;
    using i64 = int64_t;

    using byte = u8;

    using page_id = u64;
}