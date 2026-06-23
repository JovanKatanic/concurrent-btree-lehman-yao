#pragma once

#include "common.hpp"

#include <span>
#include <cstring>
#include <utf8proc.h>

namespace db7
{
    struct KeyNormEncoder
    {
    private:
        template <typename T>
        static T ByteSwapIfLittleEndian(T val)
        {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
            if constexpr (sizeof(T) == 1)
                return val;
            if constexpr (sizeof(T) == 2)
                return __builtin_bswap16(val);
            if constexpr (sizeof(T) == 4)
                return __builtin_bswap32(val);
            if constexpr (sizeof(T) == 8)
                return __builtin_bswap64(val);
#endif
            return val;
        }

        static u32 EncodeStringNormalized(byte *buf, std::span<const byte> data, bool is_case_sensitive)
        {
            utf8proc_option_t opts = static_cast<utf8proc_option_t>(
                UTF8PROC_DECOMPOSE | // NFD
                UTF8PROC_STABLE      //| UTF8PROC_NULLTERM // canonical ordering
            );

            if (!is_case_sensitive)
                opts = static_cast<utf8proc_option_t>(opts | UTF8PROC_CASEFOLD);

            utf8proc_uint8_t *output = nullptr;
            utf8proc_ssize_t out_len = utf8proc_map( // TODO this allocates
                reinterpret_cast<const utf8proc_uint8_t *>(data.data()),
                static_cast<utf8proc_ssize_t>(data.size()),
                &output,
                opts);

            if (out_len < 0 || output == nullptr)
            {
                throw std::runtime_error(std::string("utf8proc_map failed: ") + utf8proc_errmsg(out_len));
            }

            std::memcpy(buf, output, static_cast<size_t>(out_len));
            buf[out_len] = 0x00;
            free(output); // TODO this allocates

            return static_cast<u32>(out_len + 1);
        }

    public:
        template <typename T>
        static u32 Encode(byte *buf, T data, bool is_data_null, bool is_nullable, bool is_case_sensitive)
        {
            u32 size = 0;

            if (is_nullable)
            {
                buf[0] = (is_data_null) ? 0x00 : 0x01;
                size++;
                if (is_data_null)
                    return size;
                buf++;
            }

            if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
            { // i32, i64...
                using UTyp = std::make_unsigned_t<T>;
                UTyp u;
                std::memcpy(&u, &data, sizeof(T));
                u ^= (UTyp(1) << (sizeof(UTyp) * 8 - 1));
                UTyp swapped = ByteSwapIfLittleEndian(u);
                std::memcpy(buf, &swapped, sizeof(UTyp));
                size += sizeof(T);
            }
            else if constexpr (std::is_unsigned_v<T>)
            { // u32, u64...
                T swapped = ByteSwapIfLittleEndian(data);
                std::memcpy(buf, &swapped, sizeof(T));
                size += sizeof(T);
            }
            else if constexpr (std::is_same_v<T, double>)
            { // double
                uint64_t u;
                std::memcpy(&u, &data, sizeof(u));
                uint64_t mask = (u >> 63) ? ~u64(0) : (u64(1) << 63);
                u ^= mask;
                u = ByteSwapIfLittleEndian(u);
                std::memcpy(buf, &u, sizeof(u));
                size += sizeof(T);
            }
            else if constexpr (std::is_same_v<T, std::span<const byte>> || std::is_same_v<T, std::span<byte>>)
            { // strings, bytes ...
                // TODO can be optimized heavily for ascii
                size += EncodeStringNormalized(buf, data, is_case_sensitive);
            }
            else
            {
                DB7_UNREACHABLE();
            }

            return size;
        }
    };

}
