#include "layouts/varlen/varlen_layout_intermediate.hpp"

namespace db7
{
    using R = page_id;

    VarlenHeader *BtreeVarlenLayoutIntermediate::CastHeader(byte *data)
    {
        return reinterpret_cast<VarlenHeader *>(data);
    }

    void BtreeVarlenLayoutIntermediate::WriteHeader(VarlenHeader *header, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len)
    {
        header->pid = pid;
        header->rlink = rlink;
        header->llink = llink;
        header->count = count;
        header->level = level;
        header->max_val = max_val;

        header->prefix_offset = prefix_offset;
        header->prefix_len = prefix_len;
    }

    void BtreeVarlenLayoutIntermediate::WriteHeader(byte *data, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len)
    {
        auto *header = CastHeader(data);
        WriteHeader(header, pid, rlink, llink, count, level, max_val, prefix_offset, prefix_len);
    }

    /**
     * negative → slot < key
     * zero → slot == key
     * positive → slot > key
     */
    int BtreeVarlenLayoutIntermediate::Cmp(byte *slot, Key key)
    {
        SlotVal<R> val = CastSlot(slot);
        u16 len = val.hdr.len;
        u32 min_len = std::min(key.enc_len, len);
        int cmp = std::memcmp(val.data, key.encoded, min_len);
        if (cmp != 0)
            return cmp;
        return (key.enc_len < len) - (key.enc_len > len);
    }

    int BtreeVarlenLayoutIntermediate::Cmp(Key slot, Key key)
    {
        u32 min_len = std::min(key.enc_len, slot.enc_len);
        int cmp = std::memcmp(slot.encoded, key.encoded, min_len);
        if (cmp != 0)
            return cmp;
        return (key.enc_len < slot.enc_len) - (key.enc_len > slot.enc_len);
    }

    byte *BtreeVarlenLayoutIntermediate::ReadSlot(byte *data, Slot slot)
    {
        return data + slot.offset;
    }

    byte *BtreeVarlenLayoutIntermediate::ReadSlot(byte *data, u32 offset)
    {
        return data + offset;
    }

    SlotValHeader<R> *BtreeVarlenLayoutIntermediate::CastSlotHeader(void *slot)
    {
        return reinterpret_cast<SlotValHeader<R> *>(slot);
    }

    SlotVal<R> BtreeVarlenLayoutIntermediate::CastSlot(void *slot)
    {
        auto hdr = *CastSlotHeader(slot);
        return SlotVal{hdr, static_cast<byte *>(slot) + sizeof(hdr)};
    }

    Slot *BtreeVarlenLayoutIntermediate::OffsetHeader(byte *data)
    {
        return reinterpret_cast<Slot *>(data + key_offset_);
    }

    u32 BtreeVarlenLayoutIntermediate::CalcWorstCaseSize(Key key)
    {
        return key.enc_len + sizeof(SlotValHeader<R>) + alignof(SlotValHeader<R>);
    }

    u32 BtreeVarlenLayoutIntermediate::GetIdx(byte *data, const u32 count, const Key key)
    {
        Slot *slots = OffsetHeader(data);
        u32 lo = 0, hi = count;
        while (lo < hi)
        {
            u32 mid = lo + (hi - lo) / 2;
            int res = Cmp(ReadSlot(data, slots[mid]), key);
            if (res <= 0)
                lo = mid + 1;
            else
                hi = mid;
        }
        return lo;
    }

    void BtreeVarlenLayoutIntermediate::UpdateHeapSize(byte *data, u32 val)
    {
        DB7_ASSERT(val < DB7_PAGE_SIZE, "corrupted val");
        VarlenHeader *hdr = CastHeader(data);
        hdr->heap_size = val;
    }

    /* includes slot header */
    u32 BtreeVarlenLayoutIntermediate::ReserveSlot(byte *data, u32 len)
    {
        u32 heap_size = CastHeader(data)->heap_size;
        DB7_ASSERT(heap_size < DB7_PAGE_SIZE, "heap overflow");
        u32 off = AlignDown(DB7_PAGE_SIZE - heap_size - len - sizeof(SlotValHeader<R>), alignof(SlotValHeader<R>));
        UpdateHeapSize(data, DB7_PAGE_SIZE - off);
        return off;
    }

    /* doesnt include slot header */
    u32 BtreeVarlenLayoutIntermediate::ReserveSlotRaw(byte *data, u32 len)
    {
        u32 heap_size = CastHeader(data)->heap_size;
        DB7_ASSERT(heap_size < DB7_PAGE_SIZE, "heap overflow");
        u32 off = DB7_PAGE_SIZE - heap_size - len;
        UpdateHeapSize(data, DB7_PAGE_SIZE - off);
        return off;
    }

    /* Insert to heap */
    u32 BtreeVarlenLayoutIntermediate::AppendHeap(byte *data, Key key, R value)
    {
        u32 off = ReserveSlot(data, key.enc_len);

        DB7_ASSERT(off > 0 && off < DB7_PAGE_SIZE, "heap overflow");

        SlotValHeader<R> *hdr = CastSlotHeader(data + off);
        hdr->result = value;
        hdr->len = key.enc_len;
        std::memcpy(data + off + sizeof(SlotValHeader<R>), key.encoded, key.enc_len);

        return off;
    }

    u32 BtreeVarlenLayoutIntermediate::FindSplitPoint(byte *data, Slot *slots, u32 count)
    {
        VarlenHeader *var_hdr = CastHeader(data);
        u32 target = var_hdr->heap_size / 2;
        u32 accumulated = 0;

        for (u32 i = 0; i < count; i++)
        {
            SlotValHeader<R> *hdr = CastSlotHeader(ReadSlot(data, slots[i]));
            u32 entry_size = AlignUp(
                (u32)(sizeof(SlotValHeader<R>) + hdr->len),
                (u32)alignof(SlotValHeader<R>));
            accumulated += entry_size;
            if (accumulated >= target)
                return i + 1;
        }

        DB7_UNREACHABLE();
    }

    void BtreeVarlenLayoutIntermediate::CompactHeap(byte *data, Slot *slots, u32 count, u32 prefix_len)
    {
        // sort slot indices by offset descending (highest first = end of page)
        u32 indices[count];
        for (u32 i = 0; i < count; i++)
            indices[i] = i;

        std::sort(indices, indices + count, [&](u32 a, u32 b)
                  { return slots[a].offset > slots[b].offset; });

        u32 write_pos = 0;
        for (u32 i = 0; i < count; i++)
        {
            u32 idx = indices[i];
            SlotVal<R> val = CastSlot(ReadSlot(data, slots[idx]));

            u32 entry_size = sizeof(SlotValHeader<R>) + val.hdr.len - prefix_len;
            u32 off = AlignDown(DB7_PAGE_SIZE - write_pos - entry_size, alignof(SlotValHeader<R>));

            std::memmove(data + off + sizeof(SlotValHeader<R>), val.data + prefix_len, val.hdr.len - prefix_len);
            auto *new_hdr = CastSlotHeader(data + off);
            new_hdr->len = val.hdr.len - prefix_len;
            new_hdr->result = val.hdr.result;

            slots[idx].offset = off;
            write_pos = DB7_PAGE_SIZE - off;
        }

        UpdateHeapSize(data, write_pos);
    }

    Key BtreeVarlenLayoutIntermediate::ReadKey(byte *data, Slot slot)
    {
        byte *ptr = ReadSlot(data, slot);
        SlotValHeader<R> *hdr = CastSlotHeader(ptr);
        return MakeEncodedKey(hdr->len, ptr + sizeof(SlotValHeader<R>));
    }

    Key BtreeVarlenLayoutIntermediate::ReadKey(byte *data, u32 offset)
    {
        return ReadKey(data, Slot{offset});
    }

    R BtreeVarlenLayoutIntermediate::ReadMaxVal(VarlenHeader *header)
    {
        return header->max_val;
    }

    void BtreeVarlenLayoutIntermediate::InsertInternal(byte *data, u32 count, Key key, R value)
    {
        Slot *slots = OffsetHeader(data);

        /* Insert to heap */
        u32 off = AppendHeap(data, key, value);

        /* Insert slot */
        u32 idx = GetIdx(data, count, key);
        Slot slot = Slot{off};
        ShiftRightInsert(slots, count, idx, slot);
    }

    Key BtreeVarlenLayoutIntermediate::OffsetCommonPrefix(byte *data, u32 len, u32 prefix_len)
    {
        DB7_ASSERT(len >= prefix_len, "Invalid resulting len");
        DB7_ASSERT(len - prefix_len <= std::numeric_limits<u16>::max(), "key too long");
        return MakeEncodedKey((u16)(len - prefix_len), data + prefix_len);
    }

    Key BtreeVarlenLayoutIntermediate::OffsetCommonPrefix(Key key, u32 prefix_len)
    {
        DB7_ASSERT(key.enc_len >= prefix_len, "Invalid resulting len");
        DB7_ASSERT(key.enc_len - prefix_len <= std::numeric_limits<u16>::max(), "key too long");
        return MakeEncodedKey((u16)(key.enc_len - prefix_len), key.encoded + prefix_len);
    }

    u32 BtreeVarlenLayoutIntermediate::CommonPrefixLen(const byte *a, const byte *b, u32 len_a, u32 len_b)
    {
        u32 i = 0;
        u32 max_len = std::min(len_a, len_b);
        while (i < max_len && a[i] == b[i])
            i++;
        return i;
    }

    SlotVal<R> BtreeVarlenLayoutIntermediate::GetMaxValSlot(byte *data)
    {
        auto header = CastHeader(data);
        if (header->max_val != UNDEFINED)
        {
            return CastSlot(ReadSlot(data, header->max_val));
        }
        else
        {
            return SlotVal<R>{SlotValHeader<R>{0, 0}, nullptr};
        }
    }

    std::unique_ptr<byte[]> BtreeVarlenLayoutIntermediate::CopyPrefixSlot(byte *data)
    {
        auto header = CastHeader(data);
        if (header->prefix_offset != UNDEFINED_OFFSET)
        {
            byte *ptr = ReadSlot(data, header->prefix_offset);
            auto old_prefix_copy = std::make_unique<byte[]>(header->prefix_len);
            std::memcpy(old_prefix_copy.get(), ptr, header->prefix_len);
            return old_prefix_copy;
        }
        return nullptr;
    }

    Key BtreeVarlenLayoutIntermediate::RemoveKeyPrefix(byte *data, Key key)
    {
        auto *header = CastHeader(data);
        u16 len = header->prefix_len;
        DB7_ASSERT(key.enc_len >= len, "key shorter than page prefix");
        return MakeEncodedKey(static_cast<u16>(key.enc_len - len), key.encoded + len);
    }

    R BtreeVarlenLayoutIntermediate::Get(byte *data, const u32 count, const Key key)
    {
        Key new_key = RemoveKeyPrefix(data, key);

        Slot *slots = OffsetHeader(data);
        u32 idx = GetIdx(data, count, new_key);

        DB7_ASSERT(idx > 0, "key routes before leftmost entry");
        SlotVal val = CastSlot(ReadSlot(data, slots[idx - 1]));
        return val.hdr.result;
    }

    void BtreeVarlenLayoutIntermediate::Insert(byte *data, Key key, R value)
    {
        DB7_ASSERT(key.encoded != nullptr, "invalid key");
        DB7_ASSERT(key.enc_len != 0, "invalid key");
        DB7_ASSERT(key.data == nullptr && key.len == 0, "invalid key");

        u32 count = CastHeader(data)->count;
        Key new_key = RemoveKeyPrefix(data, key);
        InsertInternal(data, count, new_key, value);
        CastHeader(data)->count++;

        delete[] key.encoded;
    }

    bool BtreeVarlenLayoutIntermediate::HasSpace(byte *data, Key key)
    {
        DB7_ASSERT(key.encoded != nullptr, "invalid key");
        DB7_ASSERT(key.enc_len != 0, "invalid key");
        DB7_ASSERT(key.data == nullptr && key.len == 0, "invalid key");

        Key new_key = RemoveKeyPrefix(data, key);
        auto *hdr = CastHeader(data); // TODO fix this, this can all fit into taken_space
        return key_offset_ + hdr->count * sizeof(Slot) + hdr->heap_size + CalcWorstCaseSize(new_key) < DB7_PAGE_SIZE;
    }

    bool BtreeVarlenLayoutIntermediate::HasSplit(byte *data, Key key)
    {
        DB7_ASSERT(key.encoded != nullptr, "invalid key");
        DB7_ASSERT(key.enc_len != 0, "invalid key");

        auto *header = CastHeader(data);
        if (ReadMaxVal(header) == UNDEFINED)
            return false; // rightmost page, no high key
        auto *slot = ReadSlot((byte *)header, header->max_val);
        DB7_ASSERT((Cmp(slot, key) <= 0) == false, "node has split(this is for single thread only)"); // TODO comment
        return Cmp(slot, key) <= 0;
    }

    /**
     * TODO might be better to use thread local buffer for this case
     * to avoid copying objects
     */
    Key BtreeVarlenLayoutIntermediate::Split(byte *left_data, byte *right_data, page_id new_pid, Key key, R value)
    {
        DB7_ASSERT(key.encoded != nullptr, "invalid key");
        DB7_ASSERT(key.enc_len != 0, "invalid key");
        DB7_ASSERT(key.data == nullptr && key.len == 0, "invalid key");

        UpdateHeapSize(right_data, 0);

        auto *left_header = CastHeader(left_data);

        auto *right_header = CastHeader(right_data);

        Slot *left_slots = OffsetHeader(left_data);

        Slot *right_slots = OffsetHeader(right_data);

        u32 count = left_header->count;
        DB7_ASSERT(count != 0, "nothing to copy");

        /* copy half elements to right node */
        u32 split = FindSplitPoint(left_data, left_slots, count);
        DB7_ASSERT(split > 0 && split < count, "split must leave tuples on both sides");

        /* copy prefix value since we use it after compaction so it does not get corrupted */
        std::unique_ptr<byte[]> prefix_copy = CopyPrefixSlot(left_data);
        u16 prefix_copy_len = left_header->prefix_len;
        DB7_ASSERT((prefix_copy == nullptr) == (prefix_copy_len == 0), "prefix inconsistency");

        /* copy sentinel value since we use it after compaction so it does not get corrupted */
        Key tmp_sentinel = ReadKey(left_data, left_slots[split]);
        u16 len = tmp_sentinel.enc_len + prefix_copy_len;
        auto sentinel_buf = new byte[len];
        std::memcpy(sentinel_buf, prefix_copy.get(), prefix_copy_len);
        std::memcpy(sentinel_buf + prefix_copy_len, tmp_sentinel.encoded, tmp_sentinel.enc_len);
        Key sentinel = MakeEncodedKey(len, sentinel_buf);

        DB7_ASSERT(sentinel.enc_len >= 0, "invalid len");

        u16 prefix_len_l = 0;
        if (left_header->llink != UNDEFINED)
        {
            auto fence_l_val = CastSlot(ReadSlot(left_data, left_slots[0]));
            prefix_len_l = CommonPrefixLen(fence_l_val.data, tmp_sentinel.encoded, fence_l_val.hdr.len, tmp_sentinel.enc_len);
        }
        u16 left_prefix_len = prefix_copy_len + prefix_len_l;

        u16 prefix_len_r = 0;
        if (left_header->rlink != UNDEFINED)
        {
            /* we need to strip prefix since max val is not prefix truncated */
            auto fence_r_val = GetMaxValSlot(left_data);
            fence_r_val.data += left_header->prefix_len;
            fence_r_val.hdr.len -= left_header->prefix_len;

            prefix_len_r = CommonPrefixLen(tmp_sentinel.encoded, fence_r_val.data, tmp_sentinel.enc_len, fence_r_val.hdr.len);
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
            std::memcpy(right_data + right_prefix, sentinel.encoded, right_prefix_len);
        }

        /* append max val from left node to right */
        u64 right_max = UNDEFINED;
        if (left_header->max_val != UNDEFINED)
        {
            SlotVal val = CastSlot(ReadSlot(left_data, left_header->max_val));
            right_max = (u64)AppendHeap(right_data, MakeEncodedKey(static_cast<u16>(val.hdr.len), val.data), val.hdr.result);
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
            std::memcpy(left_data + left_prefix, sentinel.encoded, left_prefix_len);
        }

        /* finally insert main key */
        u32 left_header_count = split;
        u32 right_header_count = left_header->count - split;
        if (Cmp(sentinel, key) > 0)
        {
            auto new_key = OffsetCommonPrefix(key, left_prefix_len);
            InsertInternal(left_data, left_header_count++, new_key, value);
        }
        else
        {
            auto new_key = OffsetCommonPrefix(key, right_prefix_len);
            InsertInternal(right_data, right_header_count++, new_key, value);
        }
        delete[] key.encoded;

        /* update headers */
        WriteHeader(right_header, new_pid, left_header->rlink, left_header->pid, right_header_count, left_header->level, right_max, right_prefix, right_prefix_len);

        WriteHeader(left_header, left_header->pid, new_pid, left_header->llink, left_header_count, left_header->level, left_max, left_prefix, left_prefix_len);

        SlotVal<R> test_left;
        SlotVal<R> test_right;
        if (left_max != UNDEFINED)
            test_left = CastSlot(ReadSlot(left_data, left_max));
        if (right_max != UNDEFINED)
            test_right = CastSlot(ReadSlot(right_data, right_max));

        return sentinel;
    }

    void BtreeVarlenLayoutIntermediate::CreateRoot(byte *data, Key key, page_id pid, page_id new_pid)
    {
        DB7_ASSERT(key.encoded != nullptr, "invalid key");
        DB7_ASSERT(key.enc_len != 0, "invalid key");
        DB7_ASSERT(key.data == nullptr && key.len == 0, "invalid key");

        UpdateHeapSize(data, 0);

        Slot *slots = OffsetHeader(data);

        slots[0] = Slot{AppendHeap(data, Key{}, pid)};

        slots[1] = Slot{AppendHeap(data, key, new_pid)};

        CastHeader(data)->count = 2;

        delete[] key.encoded;
    }

    void BtreeVarlenLayoutIntermediate::InitHeader(byte *data, u32 count, u8 level, page_id pid)
    {
        WriteHeader(data, pid, UNDEFINED, UNDEFINED, count, level, UNDEFINED, UNDEFINED_OFFSET, 0);
    }

    u64 BtreeVarlenLayoutIntermediate::GetRLink(byte *data)
    {
        return CastHeader(data)->rlink;
    }
}