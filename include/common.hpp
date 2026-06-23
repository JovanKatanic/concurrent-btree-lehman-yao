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

    enum class type_id : u8
    {
        // Boolean
        BOOLEAN,

        // Integers
        TINYINT,  // int8
        SMALLINT, // int16
        INTEGER,  // int32
        BIGINT,   // int64

        // Unsigned integers
        UTINYINT,  // uint8
        USMALLINT, // uint16
        UINTEGER,  // uint32
        UBIGINT,   // uint64

        DOUBLE,

        VARCHAR,
        VARBINARY,
    };

    constexpr u8 SizeOf(type_id t)
    {
        switch (t)
        {
        case type_id::BOOLEAN:
        case type_id::TINYINT:
        case type_id::UTINYINT:
            return 1;
        case type_id::SMALLINT:
        case type_id::USMALLINT:
            return 2;
        case type_id::INTEGER:
        case type_id::UINTEGER:
            return 4;
        case type_id::BIGINT:
        case type_id::UBIGINT:
        case type_id::DOUBLE:
            return 8;
        case type_id::VARCHAR:
        case type_id::VARBINARY:
            return 16; // VarlenEntry size
        default:
            DB7_UNREACHABLE();
        }
    }
}