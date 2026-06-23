#include "layouts/varlen/varlen_layout_leaf.hpp"

namespace db7
{
    using R = u64;

    VarlenHeader *BtreeVarlenLayoutLeaf::CastHeader(byte *data)
    {
        return reinterpret_cast<VarlenHeader *>(data);
    }

    void BtreeVarlenLayoutLeaf::WriteHeader(VarlenHeader *header, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len)
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

    void BtreeVarlenLayoutLeaf::WriteHeader(byte *data, page_id pid, u64 rlink, u64 llink, u32 count, u8 level, u64 max_val, u32 prefix_offset, u16 prefix_len)
    {
        auto *header = CastHeader(data);
        WriteHeader(header, pid, rlink, llink, count, level, max_val, prefix_offset, prefix_len);
    }

    int BtreeVarlenLayoutLeaf::Cmp(byte *slot, Key key)
    {
        SlotVal<R> val = CastSlot(slot);
        u16 len = val.hdr.len;
        u32 min_len = std::min(key.len, len);
        int cmp = std::memcmp(val.data, key.data, min_len);
        if (cmp != 0)
            return cmp;
        return (key.len < len) - (key.len > len);
    }

    int BtreeVarlenLayoutLeaf::Cmp(Key slot, Key key)
    {
        u32 min_len = std::min(key.len, slot.len);
        int cmp = std::memcmp(slot.data, key.data, min_len);
        if (cmp != 0)
            return cmp;
        return (key.len < slot.len) - (key.len > slot.len);
    }

    byte *BtreeVarlenLayoutLeaf::ReadSlot(byte *data, Slot slot)
    {
        return data + slot.offset;
    }

    byte *BtreeVarlenLayoutLeaf::ReadSlot(byte *data, u32 offset)
    {
        return data + offset;
    }

    SlotValHeader<R> *BtreeVarlenLayoutLeaf::CastSlotHeader(void *slot)
    {
        return reinterpret_cast<SlotValHeader<R> *>(slot);
    }

    SlotVal<R> BtreeVarlenLayoutLeaf::CastSlot(void *slot)
    {
        auto hdr = *CastSlotHeader(slot);
        return SlotVal{hdr, static_cast<byte *>(slot) + sizeof(hdr)};
    }

    Slot *BtreeVarlenLayoutLeaf::OffsetHeader(byte *data)
    {
        return reinterpret_cast<Slot *>(data + key_offset_);
    }

    u32 BtreeVarlenLayoutLeaf::CalcWorstCaseSize(Key key)
    {
        return key.len + sizeof(SlotValHeader<R>) + alignof(SlotValHeader<R>);
    }

    u32 BtreeVarlenLayoutLeaf::GetIdx(byte *data, const u32 count, const Key key, bool &found)
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

    void BtreeVarlenLayoutLeaf::UpdateHeapSize(byte *data, u32 val)
    {
        VarlenHeader *hdr = CastHeader(data);
        hdr->heap_size = val;
    }

    /* includes slot header */
    u32 BtreeVarlenLayoutLeaf::ReserveSlot(byte *data, u32 len)
    {
        u32 heap_size = CastHeader(data)->heap_size;
        DB7_ASSERT(heap_size < DB7_PAGE_SIZE, "heap overflow");
        u32 off = AlignDown(DB7_PAGE_SIZE - heap_size - len - sizeof(SlotValHeader<R>), alignof(SlotValHeader<R>));
        UpdateHeapSize(data, DB7_PAGE_SIZE - off);
        return off;
    }

    /* doesnt include slot header */
    u32 BtreeVarlenLayoutLeaf::ReserveSlotRaw(byte *data, u32 len)
    {
        u32 heap_size = CastHeader(data)->heap_size;
        DB7_ASSERT(heap_size < DB7_PAGE_SIZE, "heap overflow");
        u32 off = DB7_PAGE_SIZE - heap_size - len;
        UpdateHeapSize(data, DB7_PAGE_SIZE - off);
        return off;
    }

    /* Insert to heap */
    u32 BtreeVarlenLayoutLeaf::AppendHeap(byte *data, Key key, R value)
    {
        u32 off = ReserveSlot(data, key.len);

        DB7_ASSERT(off > 0 && off < DB7_PAGE_SIZE, "heap overflow");

        SlotValHeader<R> *hdr = CastSlotHeader(data + off);
        hdr->result = value;
        hdr->len = key.len;
        std::memcpy(data + off + sizeof(SlotValHeader<R>), key.data, key.len);

        return off;
    }

    u32 BtreeVarlenLayoutLeaf::FindSplitPoint(byte *data, Slot *slots, u32 count)
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

    void BtreeVarlenLayoutLeaf::CompactHeap(byte *data, Slot *slots, u32 count, u32 prefix_len)
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

    Key BtreeVarlenLayoutLeaf::ReadKey(byte *data, Slot slot)
    {
        byte *ptr = ReadSlot(data, slot);
        SlotValHeader<R> *hdr = CastSlotHeader(ptr);
        return Key{hdr->len, ptr + sizeof(SlotValHeader<R>)};
    }

    Key BtreeVarlenLayoutLeaf::ReadKey(byte *data, u32 offset)
    {
        return ReadKey(data, Slot{offset});
    }

    Key BtreeVarlenLayoutLeaf::DeepCopyEncoded(Key key)
    {
        u32 BASE_OVERHEAD = key.len * 10; // TODO store this somewhere, in the tree for example based on schema
        byte *sentinel_copy = new byte[key.len + BASE_OVERHEAD];
        u32 size = KeyNormEncoder::Encode(sentinel_copy, std::span<const byte>{key.data, key.len}, key.data == nullptr, false, false);
        return MakeEncodedKey(size, sentinel_copy);
    }

    Key BtreeVarlenLayoutLeaf::RemoveKeyPrefix(byte *data, Key key)
    {
        auto *header = CastHeader(data);
        u16 len = header->prefix_len;
        DB7_ASSERT(key.enc_len >= len, "key shorter than page prefix");
        return Key{static_cast<u16>(key.len - len), key.data + len};
    }

    void BtreeVarlenLayoutLeaf::InsertInternal(byte *data, u32 count, Key key, R value)
    {
        Slot *slots = OffsetHeader(data);

        /* Insert to heap */
        u32 off = AppendHeap(data, key, value);

        /* Insert slot */
        bool found = false;
        u32 idx = GetIdx(data, count, key, found);
        if (found)
        {
            throw std::runtime_error("Key already exists");
        }
        Slot slot = Slot{off};
        ShiftRightInsert(slots, count, idx, slot); // TODO this should increment header count
    }

    R BtreeVarlenLayoutLeaf::ReadMaxVal(VarlenHeader *header)
    {
        return header->max_val;
    }

    std::unique_ptr<byte[]> BtreeVarlenLayoutLeaf::CopyPrefixSlot(byte *data)
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

    Key BtreeVarlenLayoutLeaf::OffsetCommonPrefix(byte *data, u16 len, u16 prefix_len)
    {
        DB7_ASSERT(len >= prefix_len, "Invalid resulting len");
        DB7_ASSERT(len - prefix_len <= std::numeric_limits<u16>::max(), "key too long");
        return Key{(u16)(len - prefix_len), data + prefix_len};
    }

    Key BtreeVarlenLayoutLeaf::OffsetCommonPrefix(Key key, u16 prefix_len)
    {
        DB7_ASSERT(key.len >= prefix_len, "Invalid resulting len");
        DB7_ASSERT(key.len - prefix_len <= std::numeric_limits<u16>::max(), "key too long");
        return Key{(u16)(key.len - prefix_len), key.data + prefix_len};
    }

    u32 BtreeVarlenLayoutLeaf::CommonPrefixLen(const byte *a, const byte *b, u32 len_a, u32 len_b)
    {
        u32 i = 0;
        u32 max_len = std::min(len_a, len_b);
        while (i < max_len && a[i] == b[i])
            i++;
        return i;
    }

    SlotVal<R> BtreeVarlenLayoutLeaf::GetMaxValSlot(byte *data)
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

    R BtreeVarlenLayoutLeaf::Get(byte *data, const u32 count, const Key key)
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
            return val.hdr.result;
        }
        return UNDEFINED;
    }

    void BtreeVarlenLayoutLeaf::Insert(byte *data, Key key, R value)
    {
        DB7_ASSERT(key.data != nullptr, "invalid key");
        DB7_ASSERT(key.len != 0, "invalid key");

        u32 count = CastHeader(data)->count;
        Key new_key = RemoveKeyPrefix(data, key);
        InsertInternal(data, count, new_key, value);
        CastHeader(data)->count++;
    }

    bool BtreeVarlenLayoutLeaf::HasSpace(byte *data, Key key)
    {
        DB7_ASSERT(key.data != nullptr, "invalid key");
        DB7_ASSERT(key.len != 0, "invalid key");

        Key new_key = RemoveKeyPrefix(data, key);
        auto hdr = CastHeader(data); // TODO fix this, this can all fit into taken_space
        return key_offset_ + hdr->count * sizeof(Slot) + hdr->heap_size + CalcWorstCaseSize(new_key) < DB7_PAGE_SIZE;
    }

    bool BtreeVarlenLayoutLeaf::HasSplit(byte *data, Key key)
    {
        DB7_ASSERT(key.data != nullptr, "invalid key");
        DB7_ASSERT(key.len != 0, "invalid key");

        auto *header = CastHeader(data);
        if (ReadMaxVal(header) == UNDEFINED)
            return false; // rightmost page, no high key
        auto *slot = ReadSlot((byte *)header, header->max_val);
        // DB7_ASSERT((Cmp(slot, key) <= 0) == false, "node has split(this is for single thread only)"); // TODO comment
        return Cmp(slot, key) <= 0;
    }

    /**
     * TODO might be better to use thread local buffer for this case
     * to avoid copying objects
     */
    Key BtreeVarlenLayoutLeaf::Split(byte *left_data, byte *right_data, page_id new_pid, Key key, R value)
    {
        DB7_ASSERT(key.data != nullptr, "invalid key");
        DB7_ASSERT(key.len != 0, "invalid key");

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
        if (left_header->max_val != UNDEFINED)
        {
            SlotVal val = CastSlot(ReadSlot(left_data, left_header->max_val));
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

        /* update headers */
        WriteHeader(right_header, new_pid, left_header->rlink, left_header->pid, right_header_count, left_header->level, right_max, right_prefix, right_prefix_len);

        WriteHeader(left_header, left_header->pid, new_pid, left_header->llink, left_header_count, left_header->level, left_max, left_prefix, left_prefix_len);

        /* encode a string (lib does the allocation) */
        // TODO can reuse existing sentinel buffer in future
        return DeepCopyEncoded(sentinel);
    }

    void BtreeVarlenLayoutLeaf::InitHeader(byte *data, u32 count, u8 level, page_id pid)
    {
        WriteHeader(data, pid, UNDEFINED, UNDEFINED, count, level, UNDEFINED, UNDEFINED_OFFSET, 0);
    }

    u64 BtreeVarlenLayoutLeaf::GetRLink(byte *data)
    {
        return CastHeader(data)->rlink;
    }
}