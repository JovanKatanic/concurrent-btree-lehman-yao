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

    template <typename Typ>
    struct VarlenHeader : public BaseLyHeader<Typ>
    {
        u32 heap_size;
        u32 prefix_offset;
        u16 prefix_len;

        static VarlenHeader<Typ> *CastHeader(byte *data)
        {
            return reinterpret_cast<VarlenHeader<Typ> *>(data);
        }

        void WriteHeader(Typ pid, Typ rlink, Typ llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len)
        {
            this->pid = pid;
            this->rlink = rlink;
            this->llink = llink;
            this->count = count;
            this->level = level;
            this->max_val = max_val;

            this->prefix_offset = prefix_offset;
            this->prefix_len = prefix_len;
        }

        static void WriteHeader(byte *data, Typ pid, Typ rlink, Typ llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len)
        {
            auto *header = CastHeader(data);
            header->WriteHeader(pid, rlink, llink, count, level, max_val, prefix_offset, prefix_len);
        }
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