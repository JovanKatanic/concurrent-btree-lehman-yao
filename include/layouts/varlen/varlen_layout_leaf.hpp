#pragma once

#include "common.hpp"
#include "utils/align_util.hpp"
#include "layouts/varlen/varlen_layout_models.hpp"
#include "utils/key_encoder.hpp"

#include <limits>
#include <algorithm>
#include <span>
#include <exception>

namespace db7
{

    class BtreeVarlenLayoutLeaf
    {
        using R = u64;

    private:
        u64 key_offset_;

        VarlenHeader *CastHeader(byte *data);

        void WriteHeader(VarlenHeader *header, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len);

        void WriteHeader(byte *data, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len);

        template <typename Typ>
        void ShiftRightInsert(Typ *data, u32 count, u32 idx, Typ value)
        {
            std::memmove(data + idx + 1, data + idx, (count - idx) * sizeof(Typ));
            data[idx] = value;
        }

        int Cmp(byte *slot, Key key);

        int Cmp(Key slot, Key key);

        byte *ReadSlot(byte *data, Slot slot);

        byte *ReadSlot(byte *data, u32 offset);

        SlotValHeader<R> *CastSlotHeader(void *slot);

        SlotVal<R> CastSlot(void *slot);

        Slot *OffsetHeader(byte *data);

        u32 CalcWorstCaseSize(Key key);

        u32 GetIdx(byte *data, const u32 count, const Key key, bool &found);

        void UpdateHeapSize(byte *data, u32 val);

        /* includes slot header */
        u32 ReserveSlot(byte *data, u32 len);

        /* doesnt include slot header */
        u32 ReserveSlotRaw(byte *data, u32 len);

        /* Insert to heap */
        u32 AppendHeap(byte *data, Key key, R value);

        u32 FindSplitPoint(byte *data, Slot *slots, u32 count);

        void CompactHeap(byte *data, Slot *slots, u32 count, u32 prefix_len);

        Key ReadKey(byte *data, Slot slot);

        Key ReadKey(byte *data, u32 offset);

        Key DeepCopyEncoded(Key key);

        Key RemoveKeyPrefix(byte *data, Key key);

        void InsertInternal(byte *data, u32 count, Key key, R value);

        R ReadMaxVal(VarlenHeader *header);

        std::unique_ptr<byte[]> CopyPrefixSlot(byte *data);

        Key OffsetCommonPrefix(byte *data, u16 len, u16 prefix_len);

        Key OffsetCommonPrefix(Key key, u16 prefix_len);

        u32 CommonPrefixLen(const byte *a, const byte *b, u32 len_a, u32 len_b);

        SlotVal<R> GetMaxValSlot(byte *data);

    public:
        static constexpr R UNDEFINED = std::numeric_limits<R>::max();
        static constexpr u32 UNDEFINED_OFFSET = std::numeric_limits<u32>::max();

        BtreeVarlenLayoutLeaf() : key_offset_(sizeof(VarlenHeader)) {}

        R Get(byte *data, const u32 count, const Key key);

        void Insert(byte *data, Key key, R value);

        bool HasSpace(byte *data, Key key);

        bool HasSplit(byte *data, Key key);

        /**
         * TODO might be better to use thread local buffer for this case
         * to avoid copying objects
         */
        Key Split(byte *left_data, byte *right_data, page_id new_pid, Key key, R value);

        void InitHeader(byte *data, u32 count, u8 level, page_id pid);

        u64 GetRLink(byte *data);
    };
}