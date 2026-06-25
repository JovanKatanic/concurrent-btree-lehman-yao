#pragma once

#include "common.hpp"
#include "utils/align_util.hpp"
#include "layouts/fixed/fixed_layout_models.hpp"

#include <exception>

namespace db7
{
    template <typename KeyTyp, typename ValTyp>
    class BtreeNumberLayoutLeaf
    {
        static_assert(std::is_arithmetic_v<KeyTyp>, "KeyTyp must be a numeric type");

    private:
        u64 key_offset_;
        u64 ref_offset_;
        u64 max_count_;

        template <typename Typ>
        void ShiftRightInsert(Typ *data, u32 count, u32 idx, Typ value)
        {
            std::memmove(data + idx + 1, data + idx, (count - idx) * sizeof(Typ));
            data[idx] = value;
        }

        template <typename Typ>
        void ShiftLeftDelete(Typ *data, u32 count, u32 idx)
        {
            std::memmove(data + idx, data + idx + 1, (count - idx - 1) * sizeof(Typ));
        }

        u32 GetIdx(const KeyTyp *data, const u32 count, const KeyTyp value, bool &found)
        {
            found = false;
            u32 lo = 0, hi = count;
            while (lo < hi)
            {
                u32 mid = lo + (hi - lo) / 2;
                if (data[mid] == value)
                {
                    found = true;
                    return mid;
                }
                if (data[mid] < value)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            return lo;
        }

        template <typename Typ>
        u32 CopyUpperHalf(Typ *from, Typ *to, u32 count)
        {
            u32 mid = (count + 1) / 2;
            std::memcpy(to, from + mid, (count - mid) * sizeof(Typ));
            return mid;
        }

        KeyTyp *OffsetKey(byte *data)
        {
            return reinterpret_cast<KeyTyp *>(data + key_offset_);
        }

        ValTyp *OffsetRef(byte *data)
        {
            return reinterpret_cast<ValTyp *>(data + ref_offset_);
        }

        KeyTyp GetKeyAt(byte *data, u32 idx)
        {
            KeyTyp *arr = OffsetKey(data);
            return arr[idx];
        }

        ResultObj<void> InsertInternal(byte *data, u32 count, KeyTyp key, ValTyp value)
        {
            bool found = false;
            u32 idx = count == 0 ? 0 : GetIdx(OffsetKey(data), count, key, found);
            if (found)
            {
                return ResultObj<void>::Fail("Key already exists\0");
            }
            ShiftRightInsert(OffsetKey(data), count, idx, key);
            ShiftRightInsert(OffsetRef(data), count, idx, value);
            return ResultObj<void>::Ok();
        }

        KeyTyp ReadMaxVal(NumberHeader<ValTyp> *header)
        {
            return static_cast<KeyTyp>(header->max_val);
        }

        ResultObj<void> DeleteInternal(byte *data, u32 count, KeyTyp key)
        {
            bool found = false;
            u32 idx = count == 0 ? 0 : GetIdx(OffsetKey(data), count, key, found);
            if (!found)
            {
                return ResultObj<void>::Fail("Key not found\0");
            }
            ShiftLeftDelete(OffsetKey(data), count, idx);
            ShiftLeftDelete(OffsetRef(data), count, idx);
            return ResultObj<void>::Ok();
        }

    public:
        static constexpr KeyTyp UNDEFINED = std::numeric_limits<KeyTyp>::max();

        BtreeNumberLayoutLeaf()
        {
            constexpr u64 pad_keys = sizeof(ValTyp) - 1;
            key_offset_ = AlignUp(sizeof(NumberHeader<ValTyp>), (u64)sizeof(KeyTyp));
            max_count_ = (DB7_PAGE_SIZE - key_offset_ - pad_keys) / (sizeof(KeyTyp) + sizeof(ValTyp));
            ref_offset_ = AlignUp(key_offset_ + max_count_ * sizeof(KeyTyp), (u64)sizeof(ValTyp));
        }

        ResultObj<ValTyp> Get(byte *data, const u32 count, const KeyTyp value)
        {
            bool found;
            u32 idx = GetIdx(OffsetKey(data), count, value, found);
            if (found)
                return {(OffsetRef(data))[idx], true};
            return {"Key not found\0", false};
        }

        ResultObj<void> Insert(byte *data, KeyTyp key, ValTyp value)
        {
            u32 count = NumberHeader<ValTyp>::CastHeader(data)->count;
            auto result = InsertInternal(data, count, key, value);
            if (result.success)
                NumberHeader<ValTyp>::CastHeader(data)->count++;
            return result;
        }

        bool HasSpace(byte *data, KeyTyp key)
        {
            (void)key;
            auto *header = NumberHeader<ValTyp>::CastHeader(data);
            return header->count < max_count_;
        }

        bool HasSplit(byte *data, KeyTyp key)
        {
            auto *header = NumberHeader<ValTyp>::CastHeader(data);
            return key >= ReadMaxVal(header);
        }

        ResultObj<KeyTyp> Split(byte *left_data, byte *right_data, page_id new_pid, KeyTyp key, ValTyp value)
        {
            auto *left_header = NumberHeader<ValTyp>::CastHeader(left_data);

            auto *right_header = NumberHeader<ValTyp>::CastHeader(right_data);

            u32 mid = CopyUpperHalf(OffsetKey(left_data), OffsetKey(right_data), left_header->count);

            CopyUpperHalf(OffsetRef(left_data), OffsetRef(right_data), left_header->count);

            KeyTyp sentinel = GetKeyAt(right_data, 0);

            u32 left_header_count = mid;
            u32 right_header_count = left_header->count - mid;
            ResultObj<KeyTyp> result;
            if (key < sentinel)
            {
                result = InsertInternal(left_data, left_header_count, key, value);
                if (!result.success)
                {
                    return {result.message, false};
                }
                left_header_count++;
            }
            else
            {
                result = InsertInternal(right_data, right_header_count, key, value);
                if (!result.success)
                {
                    return {result.message, false};
                }
                right_header_count++;
            }

            right_header->WriteHeader(new_pid, left_header->rlink, left_header->pid, right_header_count, left_header->level, left_header->max_val);

            left_header->WriteHeader(left_header->pid, new_pid, left_header->llink, left_header_count, left_header->level, sentinel);

            return {sentinel, true};
        }

        void InitHeader(byte *data, u32 count, u8 level, page_id pid)
        {
            NumberHeader<ValTyp>::WriteHeader(data, pid, UNDEFINED, UNDEFINED, count, level, UNDEFINED);
        }

        u64 GetRLink(byte *data)
        {
            return NumberHeader<ValTyp>::CastHeader(data)->rlink;
        }

        ResultObj<void> Delete(byte *data, KeyTyp key)
        {
            u32 count = NumberHeader<ValTyp>::CastHeader(data)->count;
            auto result = DeleteInternal(data, count, key);
            if (result.success)
                NumberHeader<ValTyp>::CastHeader(data)->count--;
            return result;
        }
    };
};