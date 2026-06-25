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
#define DB7_MAX_SLOTS_PER_PAGE (DB7_PAGE_SIZE - sizeof(VarlenHeader<ValTyp>)) / sizeof(u32)

    template <typename ValTyp>
    class BtreeVarlenLayoutLeaf
    {
    private:
        u64 key_offset_;

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

        int Cmp(byte *slot, Key key)
        {
            SlotVal<ValTyp> val = CastSlot(slot);
            u16 len = val.hdr.len;
            u32 min_len = std::min(key.len, len);
            int cmp = std::memcmp(val.data, key.data, min_len);
            if (cmp != 0)
                return cmp;
            return (key.len < len) - (key.len > len);
        }

        int Cmp(Key slot, Key key)
        {
            u32 min_len = std::min(key.len, slot.len);
            int cmp = std::memcmp(slot.data, key.data, min_len);
            if (cmp != 0)
                return cmp;
            return (key.len < slot.len) - (key.len > slot.len);
        }

        byte *ReadSlot(byte *data, Slot slot)
        {
            return data + slot.offset;
        }

        byte *ReadSlot(byte *data, u32 offset)
        {
            return data + offset;
        }

        SlotValHeader<ValTyp> *CastSlotHeader(void *slot)
        {
            return reinterpret_cast<SlotValHeader<ValTyp> *>(slot);
        }

        SlotVal<ValTyp> CastSlot(void *slot)
        {
            auto hdr = *CastSlotHeader(slot);
            return SlotVal{hdr, static_cast<byte *>(slot) + sizeof(hdr)};
        }

        Slot *OffsetHeader(byte *data)
        {
            return reinterpret_cast<Slot *>(data + key_offset_);
        }

        u32 CalcWorstCaseSize(Key key)
        {
            return key.len + sizeof(SlotValHeader<ValTyp>) + alignof(SlotValHeader<ValTyp>);
        }

        u32 GetIdx(byte *data, const u32 count, const Key key, bool &found)
        {
            Slot *slots = OffsetHeader(data);
            u32 lo = 0, hi = count;
            while (lo < hi)
            {
                u32 mid = lo + (hi - lo) / 2;
                int res = Cmp(ReadSlot(data, slots[mid]), key);
                if (res == 0)
                {
                    found = true;
                    return mid;
                }
                else if (res < 0)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            return lo;
        }

        void UpdateHeapSize(byte *data, u32 val)
        {
            auto *hdr = VarlenHeader<ValTyp>::CastHeader(data);
            hdr->heap_size = val;
        }

        /* includes slot header */
        u32 ReserveSlot(byte *data, u32 len)
        {
            u32 heap_size = VarlenHeader<ValTyp>::CastHeader(data)->heap_size;
            DB7_ASSERT(heap_size < DB7_PAGE_SIZE, "heap overflow");
            u32 off = AlignDown(DB7_PAGE_SIZE - heap_size - len - sizeof(SlotValHeader<ValTyp>), alignof(SlotValHeader<ValTyp>));
            UpdateHeapSize(data, DB7_PAGE_SIZE - off);
            return off;
        }

        /* doesnt include slot header */
        u32 ReserveSlotRaw(byte *data, u32 len)
        {
            u32 heap_size = VarlenHeader<ValTyp>::CastHeader(data)->heap_size;
            DB7_ASSERT(heap_size < DB7_PAGE_SIZE, "heap overflow");
            u32 off = DB7_PAGE_SIZE - heap_size - len;
            UpdateHeapSize(data, DB7_PAGE_SIZE - off);
            return off;
        }

        /* Insert to heap */
        u32 AppendHeap(byte *data, Key key, ValTyp value)
        {
            u32 off = ReserveSlot(data, key.len);

            DB7_ASSERT(off > 0 && off < DB7_PAGE_SIZE, "heap overflow");

            SlotValHeader<ValTyp> *hdr = CastSlotHeader(data + off);
            hdr->result = value;
            hdr->len = key.len;
            std::memcpy(data + off + sizeof(SlotValHeader<ValTyp>), key.data, key.len);

            return off;
        }

        u32 FindSplitPoint(byte *data, Slot *slots, u32 count)
        {
            auto *var_hdr = VarlenHeader<ValTyp>::CastHeader(data);
            u32 target = var_hdr->heap_size / 2;
            u32 accumulated = 0;

            for (u32 i = 0; i < count; i++)
            {
                SlotValHeader<ValTyp> *hdr = CastSlotHeader(ReadSlot(data, slots[i]));
                u32 entry_size = AlignUp(
                    (u32)(sizeof(SlotValHeader<ValTyp>) + hdr->len),
                    (u32)alignof(SlotValHeader<ValTyp>));
                accumulated += entry_size;
                if (accumulated >= target)
                    return i + 1;
            }

            DB7_UNREACHABLE();
        }

        void CompactHeap(byte *data, Slot *slots, u32 count)
        {
            auto *header = VarlenHeader<ValTyp>::CastHeader(data);

            // snapshot fence + prefix BEFORE moving anything
            bool has_max = ReadMaxVal(header) != UNDEFINED_OFFSET;
            SlotValHeader<ValTyp> max_hdr{};
            std::unique_ptr<byte[]> max_data;
            if (has_max)
            {
                SlotVal<ValTyp> mv = CastSlot(ReadSlot(data, ReadMaxVal(header)));
                max_hdr = mv.hdr;
                max_data = std::make_unique<byte[]>(mv.hdr.len);
                std::memcpy(max_data.get(), mv.data, mv.hdr.len);
            }
            bool has_prefix = header->prefix_offset != UNDEFINED_OFFSET;
            std::unique_ptr<byte[]> prefix_copy = CopyPrefixSlot(data);
            u16 prefix_len = header->prefix_len;

            u32 indices[DB7_MAX_SLOTS_PER_PAGE]; // TODO move to heap
            for (u32 i = 0; i < count; i++)
                indices[i] = i;

            std::sort(indices, indices + count, [&](u32 a, u32 b)
                      { return slots[a].offset > slots[b].offset; });

            u32 write_pos = 0;
            for (u32 i = 0; i < count; i++)
            {
                u32 idx = indices[i];
                SlotVal<ValTyp> val = CastSlot(ReadSlot(data, slots[idx]));

                u32 entry_size = sizeof(SlotValHeader<ValTyp>) + val.hdr.len; // no prefix_len
                u32 off = AlignDown(DB7_PAGE_SIZE - write_pos - entry_size,
                                    alignof(SlotValHeader<ValTyp>));

                std::memmove(data + off + sizeof(SlotValHeader<ValTyp>),
                             val.data, val.hdr.len); // verbatim
                auto *new_hdr = CastSlotHeader(data + off);
                new_hdr->len = val.hdr.len;
                new_hdr->result = val.hdr.result;

                slots[idx].offset = off;
                write_pos = DB7_PAGE_SIZE - off;
            }

            UpdateHeapSize(data, write_pos);

            // re-append fence + prefix below the packed data, same order as Split
            if (has_max)
            {
                u32 off = AppendHeap(data, Key{(u16)max_hdr.len, max_data.get()}, max_hdr.result);
                header->max_val = off; // map to your real field name
            }
            if (has_prefix)
            {
                u32 off = ReserveSlotRaw(data, prefix_len);
                std::memcpy(data + off, prefix_copy.get(), prefix_len);
                header->prefix_offset = off;
            }

            header->dead_space = 0;
        }

        void CompactHeap(byte *data, Slot *slots, u32 count, u32 prefix_len)
        {
            // sort slot indices by offset descending (highest first = end of page)
            u32 indices[DB7_MAX_SLOTS_PER_PAGE]; // TODO move to heap
            for (u32 i = 0; i < count; i++)
                indices[i] = i;

            std::sort(indices, indices + count, [&](u32 a, u32 b)
                      { return slots[a].offset > slots[b].offset; });

            u32 write_pos = 0;
            for (u32 i = 0; i < count; i++)
            {
                u32 idx = indices[i];
                SlotVal<ValTyp> val = CastSlot(ReadSlot(data, slots[idx]));

                u32 entry_size = sizeof(SlotValHeader<ValTyp>) + val.hdr.len - prefix_len;
                u32 off = AlignDown(DB7_PAGE_SIZE - write_pos - entry_size, alignof(SlotValHeader<ValTyp>));

                std::memmove(data + off + sizeof(SlotValHeader<ValTyp>), val.data + prefix_len, val.hdr.len - prefix_len);
                auto *new_hdr = CastSlotHeader(data + off);
                new_hdr->len = val.hdr.len - prefix_len;
                new_hdr->result = val.hdr.result;

                slots[idx].offset = off;
                write_pos = DB7_PAGE_SIZE - off;
            }

            UpdateHeapSize(data, write_pos);
        }

        Key ReadKey(byte *data, Slot slot)
        {
            byte *ptr = ReadSlot(data, slot);
            SlotValHeader<ValTyp> *hdr = CastSlotHeader(ptr);
            return Key{hdr->len, ptr + sizeof(SlotValHeader<ValTyp>)};
        }

        Key ReadKey(byte *data, u32 offset)
        {
            return ReadKey(data, Slot{offset});
        }

        Key DeepCopyEncoded(Key key)
        {
            u32 BASE_OVERHEAD = key.len * 10; // TODO store this somewhere, in the tree for example based on schema
            byte *sentinel_copy = new byte[key.len + BASE_OVERHEAD];
            u32 size = KeyNormEncoder::Encode(sentinel_copy, std::span<const byte>{key.data, key.len}, key.data == nullptr, false, false);
            return MakeEncodedKey(size, sentinel_copy);
        }

        Key RemoveKeyPrefix(byte *data, Key key)
        {
            auto *header = VarlenHeader<ValTyp>::CastHeader(data);
            u16 len = header->prefix_len;
            DB7_ASSERT(key.len >= len, "key shorter than page prefix");
            return Key{static_cast<u16>(key.len - len), key.data + len};
        }

        ResultObj<void> InsertInternal(byte *data, u32 count, Key key, ValTyp value)
        {
            /* Insert slot */
            bool found = false;
            u32 idx = GetIdx(data, count, key, found);
            if (found)
            {
                return ResultObj<void>::Fail("Key already exists\0");
            }

            Slot *slots = OffsetHeader(data);

            /* Insert to heap */
            u32 off = AppendHeap(data, key, value);

            Slot slot = Slot{off};
            ShiftRightInsert(slots, count, idx, slot); // TODO this should increment header count

            return ResultObj<void>::Ok();
        }

        u32 ReadMaxVal(VarlenHeader<ValTyp> *header)
        {
            return static_cast<u32>(header->max_val);
        }

        std::unique_ptr<byte[]> CopyPrefixSlot(byte *data)
        {
            auto header = VarlenHeader<ValTyp>::CastHeader(data);
            if (header->prefix_offset != UNDEFINED_OFFSET)
            {
                byte *ptr = ReadSlot(data, header->prefix_offset);
                auto old_prefix_copy = std::make_unique<byte[]>(header->prefix_len);
                std::memcpy(old_prefix_copy.get(), ptr, header->prefix_len);
                return old_prefix_copy;
            }
            return nullptr;
        }

        Key OffsetCommonPrefix(byte *data, u16 len, u16 prefix_len)
        {
            DB7_ASSERT(len >= prefix_len, "Invalid resulting len");
            DB7_ASSERT(len - prefix_len <= std::numeric_limits<u16>::max(), "key too long");
            return Key{(u16)(len - prefix_len), data + prefix_len};
        }

        Key OffsetCommonPrefix(Key key, u16 prefix_len)
        {
            DB7_ASSERT(key.len >= prefix_len, "Invalid resulting len");
            DB7_ASSERT(key.len - prefix_len <= std::numeric_limits<u16>::max(), "key too long");
            return Key{(u16)(key.len - prefix_len), key.data + prefix_len};
        }

        u32 CommonPrefixLen(const byte *a, const byte *b, u32 len_a, u32 len_b)
        {
            u32 i = 0;
            u32 max_len = std::min(len_a, len_b);
            while (i < max_len && a[i] == b[i])
                i++;
            return i;
        }

        SlotVal<ValTyp> GetMaxValSlot(byte *data)
        {
            auto header = VarlenHeader<ValTyp>::CastHeader(data);
            auto max_val = ReadMaxVal(header);
            if (max_val != UNDEFINED_OFFSET)
            {
                return CastSlot(ReadSlot(data, max_val));
            }
            else
            {
                return SlotVal<ValTyp>{SlotValHeader<ValTyp>{0, 0}, nullptr};
            }
        }

        ResultObj<void> DeleteInternal(byte *data, Key key)
        {
            auto *header = VarlenHeader<ValTyp>::CastHeader(data);
            Slot *slots = OffsetHeader(data);
            u32 count = header->count;

            bool found = false;
            u32 idx = GetIdx(data, count, key, found);
            if (!found)
            {
                return ResultObj<void>::Fail("Key not found\0");
            }

            SlotValHeader<ValTyp> *hdr = CastSlotHeader(ReadSlot(data, slots[idx]));
            header->dead_space += AlignUp((sizeof(SlotValHeader<ValTyp>) + hdr->len), alignof(SlotValHeader<ValTyp>));

            ShiftLeftDelete(slots, count, idx);

            count--;

            header->count = count;

            if (header->dead_space > DB7_PAGE_SIZE / 4)
                CompactHeap(data, slots, count);

            return ResultObj<void>::Ok();
        }

    public:
        static constexpr ValTyp UNDEFINED = std::numeric_limits<ValTyp>::max();
        static constexpr u32 UNDEFINED_OFFSET = std::numeric_limits<u32>::max();

        BtreeVarlenLayoutLeaf() : key_offset_(sizeof(VarlenHeader<ValTyp>)) {}

        ResultObj<ValTyp> Get(byte *data, const u32 count, const Key key)
        {
            DB7_ASSERT(key.data != nullptr, "invalid key");
            DB7_ASSERT(key.len != 0, "invalid key");

            Key new_key = RemoveKeyPrefix(data, key);

            Slot *slots = OffsetHeader(data);

            bool found = false;
            u32 idx = GetIdx(data, count, new_key, found);
            if (found)
            {
                SlotVal val = CastSlot(ReadSlot(data, slots[idx]));
                return {val.hdr.result, true};
            }

            return {"Key not found\0", false};
        }

        ResultObj<void> Insert(byte *data, Key key, ValTyp value)
        {
            DB7_ASSERT(key.data != nullptr, "invalid key");
            DB7_ASSERT(key.len != 0, "invalid key");

            u32 count = VarlenHeader<ValTyp>::CastHeader(data)->count;
            Key new_key = RemoveKeyPrefix(data, key);
            auto result = InsertInternal(data, count, new_key, value);
            if (result.success)
                VarlenHeader<ValTyp>::CastHeader(data)->count++;
            return result;
        }

        bool HasSpace(byte *data, Key key)
        {
            DB7_ASSERT(key.data != nullptr, "invalid key");
            DB7_ASSERT(key.len != 0, "invalid key");

            Key new_key = RemoveKeyPrefix(data, key);
            auto hdr = VarlenHeader<ValTyp>::CastHeader(data); // TODO fix this, this can all fit into taken_space
            return key_offset_ + hdr->count * sizeof(Slot) + hdr->heap_size + CalcWorstCaseSize(new_key) < DB7_PAGE_SIZE;
        }

        bool HasSplit(byte *data, Key key)
        {
            DB7_ASSERT(key.data != nullptr, "invalid key");
            DB7_ASSERT(key.len != 0, "invalid key");

            auto *header = VarlenHeader<ValTyp>::CastHeader(data);
            if (ReadMaxVal(header) == UNDEFINED_OFFSET)
                return false; // rightmost page, no high key
            auto *slot = ReadSlot((byte *)header, ReadMaxVal(header));
            // DB7_ASSERT((Cmp(slot, key) <= 0) == false, "node has split(this is for single thread only)"); // TODO comment
            return Cmp(slot, key) <= 0;
        }

        /**
         * TODO might be better to use thread local buffer for this case
         * to avoid copying objects
         */
        ResultObj<Key> Split(byte *__restrict left_data, byte *__restrict right_data, ValTyp new_pid, Key key, ValTyp value)
        {
            DB7_ASSERT(key.data != nullptr, "invalid key");
            DB7_ASSERT(key.len != 0, "invalid key");

            auto *left_header = VarlenHeader<ValTyp>::CastHeader(left_data);

            auto *right_header = VarlenHeader<ValTyp>::CastHeader(right_data);

            Slot *left_slots = OffsetHeader(left_data);

            Slot *right_slots = OffsetHeader(right_data);

            UpdateHeapSize(right_data, 0);

            u32 count = left_header->count;
            DB7_ASSERT(count != 0, "nothing to copy");

            /* copy half elements to right node */
            u32 split = FindSplitPoint(left_data, left_slots, count);
            DB7_ASSERT(split > 0 && split < count, "split must leave tuples on both sides");

            /* copy prefix value since we use it after compaction so it does not get corrupted */
            std::unique_ptr<byte[]> prefix_copy = CopyPrefixSlot(left_data);
            u16 prefix_copy_len = left_header->prefix_len;

            /* copy sentinel value since we use it after compaction so it does not get corrupted */
            Key tmp_sentinel = ReadKey(left_data, left_slots[split]);
            u16 len = tmp_sentinel.len + prefix_copy_len;

            /* copy sentinel value */
            auto sentinel_buf = std::make_unique<byte[]>(len);
            DB7_ASSERT(sentinel_buf.get() != prefix_copy.get(), "allocator aliased prefix_copy");
            std::memcpy(sentinel_buf.get(), prefix_copy.get(), prefix_copy_len);
            std::memcpy(sentinel_buf.get() + prefix_copy_len, tmp_sentinel.data, tmp_sentinel.len);
            Key sentinel = Key{len, sentinel_buf.get()};
            DB7_ASSERT(sentinel.len >= 0, "invalid len");

            u16 prefix_len_l = 0;
            if (left_header->llink != UNDEFINED)
            {
                auto fence_l_val = CastSlot(ReadSlot(left_data, left_slots[0]));
                prefix_len_l = CommonPrefixLen(fence_l_val.data, tmp_sentinel.data, fence_l_val.hdr.len, tmp_sentinel.len);
            }
            u16 left_prefix_len = prefix_copy_len + prefix_len_l;

            u16 prefix_len_r = 0;
            if (left_header->rlink != UNDEFINED)
            {
                /* we need to strip prefix since max val is not prefix truncated */
                auto fence_r_val = GetMaxValSlot(left_data);
                fence_r_val.data += left_header->prefix_len;
                fence_r_val.hdr.len -= left_header->prefix_len;

                prefix_len_r = CommonPrefixLen(tmp_sentinel.data, fence_r_val.data, tmp_sentinel.len, fence_r_val.hdr.len);
            }
            u16 right_prefix_len = prefix_copy_len + prefix_len_r;

            /* copy range of upper slots */
            for (u32 i = split; i < count; i++)
            {
                SlotVal val = CastSlot(ReadSlot(left_data, left_slots[i]));
                u32 off = AppendHeap(right_data, OffsetCommonPrefix(val.data, val.hdr.len, prefix_len_r), val.hdr.result);
                right_slots[i - split] = Slot{off};
            }

            /* append right prefixes */
            u32 right_prefix = UNDEFINED_OFFSET;
            if (left_header->rlink != UNDEFINED)
            {
                right_prefix = ReserveSlotRaw(right_data, right_prefix_len);
                std::memcpy(right_data + right_prefix, sentinel.data, right_prefix_len);
            }

            /* append max val from left node to right */
            u64 right_max = UNDEFINED;
            if (ReadMaxVal(left_header) != UNDEFINED_OFFSET)
            {
                SlotVal val = CastSlot(ReadSlot(left_data, ReadMaxVal(left_header)));
                right_max = (u64)AppendHeap(right_data, Key{static_cast<u16>(val.hdr.len), val.data}, val.hdr.result);
            }

            /* do compaction on left side */
            CompactHeap(left_data, left_slots, split, prefix_len_l);

            /* append left max val */
            u64 left_max = (u64)AppendHeap(left_data, sentinel, UNDEFINED);

            /* append left prefixes */
            u32 left_prefix = UNDEFINED_OFFSET;
            if (left_header->llink != UNDEFINED)
            {
                left_prefix = ReserveSlotRaw(left_data, left_prefix_len);
                std::memcpy(left_data + left_prefix, sentinel.data, left_prefix_len);
            }

            /* finally insert main key */
            u32 left_header_count = split;
            u32 right_header_count = left_header->count - split;
            ResultObj<void> result;
            if (Cmp(sentinel, key) > 0)
            {
                auto new_key = OffsetCommonPrefix(key, left_prefix_len);
                result = InsertInternal(left_data, left_header_count++, new_key, value);
            }
            else
            {
                auto new_key = OffsetCommonPrefix(key, right_prefix_len);
                result = InsertInternal(right_data, right_header_count++, new_key, value);
            }

            if (!result.success)
            {
                return {result.message, false};
            }

            /* update headers */
            right_header->WriteHeader(new_pid, left_header->rlink, left_header->pid, right_header_count, left_header->level, right_max, right_prefix, right_prefix_len);

            left_header->WriteHeader(left_header->pid, new_pid, left_header->llink, left_header_count, left_header->level, left_max, left_prefix, left_prefix_len);

            /* encode a string (lib does the allocation) */
            // TODO can reuse existing sentinel buffer in future
            return {DeepCopyEncoded(sentinel), true};
        }

        void InitHeader(byte *data, u32 count, u8 level, ValTyp pid)
        {
            VarlenHeader<ValTyp>::WriteHeader(data, pid, UNDEFINED, UNDEFINED, count, level, UNDEFINED_OFFSET, UNDEFINED_OFFSET, 0);
        }

        u64 GetRLink(byte *data)
        {
            return VarlenHeader<ValTyp>::CastHeader(data)->rlink;
        }

        ResultObj<void> Delete(byte *data, Key key)
        {
            DB7_ASSERT(key.data != nullptr, "invalid key");
            DB7_ASSERT(key.len != 0, "invalid key");

            Key new_key = RemoveKeyPrefix(data, key);
            return DeleteInternal(data, new_key);
        }
    };
}