#pragma once

#include "common.hpp"
#include "utils/align_util.hpp"
#include "layouts/fixed/fixed_layout_models.hpp"

namespace db7
{
    template <typename T>
    class BtreeNumberLayoutIntermediate
    {
        static_assert(std::is_arithmetic_v<T>, "T must be a numeric type");

    private:
        u64 key_offset_;
        u64 ref_offset_;
        u64 max_count_;

        NumberHeader *CastHeader(byte *data)
        {
            return reinterpret_cast<NumberHeader *>(data);
        }

        void WriteHeader(NumberHeader *header, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val)
        {
            header->pid = pid;
            header->rlink = rlink;
            header->llink = llink;
            header->count = count;
            header->level = level;
            header->max_val = max_val;
        }

        void WriteHeader(byte *data, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val)
        {
            auto *header = CastHeader(data);
            WriteHeader(header, pid, rlink, llink, count, level, max_val);
        }

        template <typename Typ>
        void ShiftRightInsert(Typ *data, u32 count, u32 idx, Typ value)
        {
            std::memmove(data + idx + 1, data + idx, (count - idx) * sizeof(Typ));
            data[idx] = value;
        }

        u32 GetIdx(const T *data, const u32 count, const T value)
        {
            DB7_ASSERT(count != 0, "zero count node");
            u32 lo = 0, hi = count;
            while (lo < hi)
            {
                u32 mid = lo + (hi - lo) / 2;
                if (data[mid] <= value)
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
            std::memcpy(to, from + mid + 1, (count - mid) * sizeof(Typ));
            return mid;
        }

        T *OffsetKey(byte *data)
        {
            return reinterpret_cast<T *>(data + key_offset_);
        }

        page_id *OffsetRef(byte *data)
        {
            return reinterpret_cast<page_id *>(data + ref_offset_);
        }

        T GetKeyAt(byte *data, u32 idx)
        {
            T *arr = OffsetKey(data);
            return arr[idx];
        }

        void InsertInternal(byte *data, u32 count, T key, page_id value)
        {
            u32 idx = GetIdx(OffsetKey(data), count, key);
            ShiftRightInsert(OffsetKey(data), count, idx, key);
            ShiftRightInsert(OffsetRef(data), count + 1, idx + 1, value);
        }

    public:
        static constexpr T UNDEFINED = std::numeric_limits<T>::max();

        BtreeNumberLayoutIntermediate()
        {
            constexpr u64 pad_keys = sizeof(page_id) - 1;
            key_offset_ = AlignUp(sizeof(NumberHeader), (u64)sizeof(T));
            max_count_ = (DB7_PAGE_SIZE - key_offset_ - pad_keys - sizeof(page_id)) / (sizeof(T) + sizeof(page_id));
            ref_offset_ = AlignUp(key_offset_ + max_count_ * sizeof(T), (u64)sizeof(page_id));
        }

        auto Get(byte *data, const u32 count, const T value)
        {
            T *arr = OffsetKey(data);
            u32 idx = GetIdx(arr, count, value);
            return OffsetRef(data)[idx];
        }

        void Insert(byte *data, T key, page_id value)
        {
            u32 count = CastHeader(data)->count;
            InsertInternal(data, count, key, value);
            CastHeader(data)->count++;
        }

        bool HasSpace(byte *data, T key)
        {
            (void)key;
            auto *header = CastHeader(data);
            return header->count < max_count_;
        }

        bool HasSplit(byte *data, T key)
        {
            auto *header = CastHeader(data);
            return key >= header->max_val;
        }

        void CreateRoot(byte *data, T key, page_id pid, page_id new_pid)
        {
            *OffsetKey(data) = key;
            *OffsetRef(data) = pid;
            *OffsetRef(data + sizeof(page_id)) = new_pid;
        }

        T Split(byte *left_data, byte *right_data, page_id new_pid, T key, page_id value)
        {
            auto *left_header = CastHeader(left_data);

            auto *right_header = CastHeader(right_data);

            u32 mid = CopyUpperHalf(OffsetKey(left_data), OffsetKey(right_data), left_header->count);

            CopyUpperHalf(OffsetRef(left_data), OffsetRef(right_data), left_header->count);

            T sentinel = GetKeyAt(left_data, mid);

            u32 left_header_count = mid;
            u32 right_header_count = left_header->count - mid - 1;

            if (key < sentinel)
            {
                InsertInternal(left_data, left_header_count, key, value);
                left_header_count++;
            }
            else
            {
                InsertInternal(right_data, right_header_count, key, value);
                right_header_count++;
            }

            right_header->rlink = left_header->rlink;
            right_header->count = right_header_count;
            right_header->level = left_header->level;
            right_header->max_val = left_header->max_val;

            left_header->rlink = new_pid;
            left_header->count = left_header_count;
            left_header->max_val = sentinel;

            return sentinel;
        }

        void InitHeader(byte *data, u32 count, u8 level, page_id pid)
        {
            WriteHeader(data, pid, UNDEFINED, UNDEFINED, count, level, UNDEFINED);
        }

        u64 GetRLink(byte *data)
        {
            return CastHeader(data)->rlink;
        }
    };
};