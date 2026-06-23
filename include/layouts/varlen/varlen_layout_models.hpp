#pragma once

#include "common.hpp"
#include "btree/header.hpp"

namespace db7
{
    struct Key
    {
        u16 len;
        byte *data;
        u16 enc_len;
        byte *encoded;

        Key() : len(0), data(nullptr), enc_len(0), encoded(nullptr) {}

        Key(u16 len, byte *data)
            : len(len), data(data), enc_len(0), encoded(nullptr) {}

        Key(u16 len, byte *data, u16 enc_len, byte *encoded)
            : len(len), data(data), enc_len(enc_len), encoded(encoded) {}
    };

    inline Key MakeEncodedKey(u16 enc_len, byte *encoded)
    {
        return Key{0, nullptr, enc_len, encoded};
    }

    struct VarlenHeader : public BaseLyHeader
    {
        u32 heap_size;
        u32 prefix_offset;
        u16 prefix_len;
    };

    struct Slot
    {
        u32 offset;
    };

    template <typename R>
    struct SlotValHeader
    {
        R result;
        u16 len;
    };

    template <typename R>
    struct SlotVal
    {
        SlotValHeader<R> hdr;
        byte *data;
    };
};