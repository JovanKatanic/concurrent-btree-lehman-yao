#pragma once

#include "common.hpp"
#include "btree/header.hpp"

namespace db7
{
    template <typename ValTyp>
    struct NumberHeader : public BaseLyHeader<ValTyp>
    {
        void WriteHeader(ValTyp pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val)
        {
            this->pid = pid;
            this->rlink = rlink;
            this->llink = llink;
            this->count = count;
            this->level = level;
            this->max_val = max_val;
        }

        static NumberHeader<ValTyp> *CastHeader(byte *data)
        {
            return reinterpret_cast<NumberHeader<ValTyp> *>(data);
        }

        static void WriteHeader(byte *data, ValTyp pid, ValTyp rlink, ValTyp llink, u32 count, u8 level, u64 max_val)
        {
            auto *header = CastHeader(data);
            header->WriteHeader(pid, rlink, llink, count, level, max_val);
        }
    };
}