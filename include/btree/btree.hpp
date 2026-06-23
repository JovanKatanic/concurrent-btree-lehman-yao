#pragma once

#include "common.hpp"
#include "utils/page_pool.hpp"
#include "utils/thread_local.hpp"
#include "btree/header.hpp"
#include "layouts/varlen/varlen_layout_intermediate.hpp"
#include "layouts/varlen/varlen_layout_leaf.hpp"

#include <atomic>
#include <mutex>

namespace db7
{
    // using Key = u64;
    // using Value = u64;
    template <typename Key, typename Value>
    class BTreeIndex
    {
        // TODO assert Value is correct type
    private:
        std::mutex root_mtx_;
        std::atomic<page_id> root_id_;
        PagePool *page_pool_;

        using InterLayout = BtreeVarlenLayoutIntermediate;
        using LeafLayout = BtreeVarlenLayoutLeaf;

        InterLayout layout_inter_;
        LeafLayout layout_leaf_;

        template <LockMode Mode>
        Page *GetNode(page_id id)
        {
            Page *page = page_pool_->Get(id);
            Lock<Mode>(page);
            return page;
        }

        template <LockMode Mode>
        void ReleaseNode(Page *page)
        {
            Unlock<Mode>(page);
            page_pool_->Return(page);
        }

        Page *ReserveNode()
        {
            return page_pool_->Reserve();
        }

        void CreateNewRoot(u8 level, Key key, page_id pid, page_id new_pid)
        {
            Page *new_root_page = ReserveNode();

            byte *new_root_data = new_root_page->GetData();

            layout_inter_.InitHeader(new_root_data, 1, level + 1, new_root_page->GetId());

            layout_inter_.CreateRoot(new_root_data, key, pid, new_pid);

            root_id_.store(new_root_page->GetId());

            ReleaseNode<LockMode::None>(new_root_page);
        }

        Key SplitLeaf(byte *data, page_id &new_pid, Key key, Value value)
        {
            auto *right_page = ReserveNode();

            new_pid = right_page->GetId();

            byte *right_data = right_page->GetData();

            Key sentinel = layout_leaf_.Split(data, right_data, new_pid, key, value);

            ReleaseNode<LockMode::None>(right_page);

            return sentinel;
        }

        Key SplitInter(byte *data, page_id &new_pid, Key key, page_id value)
        {
            auto *right_page = ReserveNode();

            new_pid = right_page->GetId();

            byte *right_data = right_page->GetData();

            Key sentinel = layout_inter_.Split(data, right_data, new_pid, key, value);

            ReleaseNode<LockMode::None>(right_page);

            return sentinel;
        }

        Page *GoRightInter(Page *page, Key key)
        {
            constexpr LockMode LM = LockMode::Write;
            do
            {
                auto *data = page->GetData();

                if (!layout_inter_.HasSplit(data, key))
                {
                    return page;
                }
                else
                {
                    page_id new_pid = layout_inter_.GetRLink(data);

                    ReleaseNode<LM>(page);

                    page = GetNode<LM>(new_pid);
                }
            } while (true);

            DB7_UNREACHABLE();
        }

        Page *GoRightLeaf(Page *page, Key key)
        {
            constexpr LockMode LM = LockMode::Write;
            do
            {
                auto *data = page->GetData();

                if (!layout_leaf_.HasSplit(data, key))
                {
                    return page;
                }
                else
                {
                    page_id new_pid = layout_leaf_.GetRLink(data);

                    ReleaseNode<LM>(page);

                    page = GetNode<LM>(new_pid);
                }
            } while (true);

            DB7_UNREACHABLE();
        }

        page_id GetRoot()
        {
            return root_id_.load();
        }

        Page *DropToLevel(Key key)
        {
            page_id pid = GetRoot();
            DB7_ASSERT(pid != std::numeric_limits<page_id>::max(), "invalid pid");

            Page *page = GetNode<LockMode::None>(pid);
            auto *data = page->GetData();
            u8 level = GetLevel(data);

            do
            {
                DB7_ASSERT(pid != std::numeric_limits<page_id>::max(), "invalid pid");
                DB7_ASSERT(level == GetLevel(data), "invalid level node");

                constexpr LockMode LM = LockMode::Optimistic;
                Lock<LM>(page);

                if (level <= 0)
                {
                    if (!Unlock<LM>(page))
                        continue;

                    return page;
                }
                else if (layout_inter_.HasSplit(data, key))
                {
                    page_id new_pid = layout_inter_.GetRLink(data);

                    if (!Unlock<LM>(page))
                        continue;

                    pid = new_pid;
                }
                else
                {
                    page_id new_pid = layout_inter_.Get(data, GetCount(data), key);

                    if (!Unlock<LM>(page))
                        continue;

                    TlState::Push(pid); // TODO should probably store a pointer and keep pages pinned
                    pid = new_pid;
                    level--;
                }

                /* Unlock prev page */
                ReleaseNode<LockMode::None>(page);

                /* Fetch a new page page */
                page = GetNode<LockMode::None>(pid);
                data = page->GetData();

            } while (true);

            DB7_UNREACHABLE();

            return nullptr;
        }

        void DropToLevel(Key key, u8 drop_level)
        {
            page_id pid = GetRoot();
            DB7_ASSERT(pid != std::numeric_limits<page_id>::max(), "invalid pid");

            Page *page = GetNode<LockMode::None>(pid);
            auto *data = page->GetData();
            u8 level = GetLevel(data);

            do
            {
                DB7_ASSERT(pid != std::numeric_limits<page_id>::max(), "invalid pid");
                DB7_ASSERT(level == GetLevel(data), "invalid level node");

                constexpr LockMode LM = LockMode::Optimistic;
                Lock<LM>(page);

                if (level <= drop_level)
                {
                    if (!Unlock<LM>(page))
                        continue;

                    TlState::Push(pid);

                    return;
                }
                else if (layout_inter_.HasSplit(data, key))
                {
                    page_id new_pid = layout_inter_.GetRLink(data);

                    if (!Unlock<LM>(page))
                        continue;

                    pid = new_pid;
                }
                else
                {
                    page_id new_pid = layout_inter_.Get(data, GetCount(data), key);

                    if (!Unlock<LM>(page))
                        continue;

                    TlState::Push(pid); // TODO should probably store a pointer and keep pages pinned
                    pid = new_pid;
                    level--;
                }

                /* Unlock prev page */
                ReleaseNode<LockMode::None>(page);

                /* Fetch a new page page */
                page = GetNode<LockMode::None>(pid);
                data = page->GetData();
            } while (true);

            DB7_UNREACHABLE();
        }

        bool InsertInternal(Page *page, Key key, Value value)
        {
            Lock<LockMode::Write>(page);

            page = GoRightLeaf(page, key);
            byte *data = page->GetData();
            page_id pid = page->GetId();

            if (layout_leaf_.HasSpace(data, key))
            {
                layout_leaf_.Insert(data, key, value);
                ReleaseNode<LockMode::Write>(page);
            }
            else
            {
                page_id new_pid;
                Key sentinel = SplitLeaf(data, new_pid, key, value);
                u8 level = GetLevel(data);
                ReleaseNode<LockMode::Write>(page);

                if (TlState::IsEmpty())
                {
                    root_mtx_.lock();
                    if (GetRoot() == pid)
                    {
                        CreateNewRoot(level, sentinel, pid, new_pid);
                        root_mtx_.unlock();
                    }
                    else
                    {
                        root_mtx_.unlock();
                        DropToLevel(sentinel, level + 1);
                        return PropagateInsert(sentinel, new_pid);
                    }
                }
                else
                {
                    return PropagateInsert(sentinel, new_pid);
                }
            }

            return true;
        }

        bool PropagateInsert(Key key, page_id value)
        {
            while (!TlState::IsEmpty())
            {
                page_id pid = TlState::Pop();
                DB7_ASSERT(pid != std::numeric_limits<page_id>::max(), "invalid pid");

                constexpr LockMode LM = LockMode::Write;
                auto *page = GetNode<LM>(pid);
                page = GoRightInter(page, key);
                pid = page->GetId();

                auto *data = page->GetData();

                if (layout_inter_.HasSpace(data, key))
                {
                    layout_inter_.Insert(data, key, value);
                    ReleaseNode<LM>(page);
                    break;
                }
                else
                {
                    page_id new_pid;

                    // Key old_key = key;
                    key = SplitInter(data, new_pid, key, value);
                    // delete[] old_key.encoded;

                    value = new_pid;
                    u8 level = GetLevel(data);
                    ReleaseNode<LM>(page);

                    if (TlState::IsEmpty())
                    {
                        root_mtx_.lock();
                        if (GetRoot() == pid)
                        {
                            CreateNewRoot(level, key, pid, new_pid);
                            root_mtx_.unlock();
                            break;
                        }
                        else
                        {
                            root_mtx_.unlock();
                            DropToLevel(key, level + 1);
                        }
                    }
                }
            }

            return true;
        }

        Value InternalGet(Page *page, Key key)
        {
            DB7_ASSERT(page->GetId() != std::numeric_limits<page_id>::max(), "invalid pid");
            auto *data = page->GetData();

            do
            {
                DB7_ASSERT(page->GetId() != std::numeric_limits<page_id>::max(), "invalid pid");

                constexpr LockMode LM = LockMode::Optimistic;
                Lock<LM>(page);

                /* Check sentinel value */
                if (layout_leaf_.HasSplit(data, key))
                {
                    page_id new_pid = layout_leaf_.GetRLink(data);

                    if (!Unlock<LM>(page))
                        continue;

                    ReleaseNode<LockMode::None>(page);
                    page = GetNode<LockMode::None>(new_pid);
                    data = page->GetData();
                }
                else
                {
                    page_id result = layout_leaf_.Get(data, GetCount(data), key);

                    if (!Unlock<LM>(page))
                        continue;

                    ReleaseNode<LockMode::None>(page);
                    return result;
                }

            } while (true);

            DB7_UNREACHABLE();
        }

    public:
        BTreeIndex(PagePool *page_pool)
            : root_id_(0), page_pool_(page_pool), layout_inter_(), layout_leaf_()
        {
            Page *page = page_pool->Reserve();

            layout_leaf_.InitHeader(page->GetData(), 0, 0, page->GetId());

            ReleaseNode<LockMode::None>(page);
        }

        ~BTreeIndex() = default;

        bool Insert(Key key, Value value)
        {
            TlState::Clear();
            Page *page = DropToLevel(key);
            return InsertInternal(page, key, value);
        }

        bool Delete(/* ... */) { return false; }

        Value Get(Key key)
        {
            TlState::Clear();
            auto *page = DropToLevel(key);
            return InternalGet(page, key);
        }
    };
}