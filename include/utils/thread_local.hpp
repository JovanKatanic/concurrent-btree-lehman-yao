#pragma once

#include "common.hpp"

namespace db7
{
    class TlState
    {
    private:
        static inline thread_local u64 version_ = 0;
        static inline thread_local u32 tries_ = 0;
        static inline thread_local page_id state_buf_[16] = {};
        static inline thread_local u32 state_size_ = 0;

    public:
        static u64 GetVersion() { return version_; }
        static void SetVersion(u64 v) { version_ = v; }

        static u32 GetTries() { return tries_; }
        static void IncrementTries() { tries_++; }

        static void Clear()
        {
            version_ = 0;
            tries_ = 0;
            state_size_ = 0;
        }

        static void Push(page_id id)
        {
            DB7_ASSERT(state_size_ < 16, "Invalid size tried to push to full stack");
            state_buf_[state_size_++] = id;
        }

        static page_id Pop()
        {
            DB7_ASSERT(state_size_ >= 1, "Invalid size tried to pop from empty stack");
            return state_buf_[--state_size_];
        }

        static bool IsEmpty()
        {
            return state_size_ == 0;
        }
    };

    constexpr u64 MAX_OPTIMISTIC_TRIES = 1;

    enum class LockMode
    {
        None,
        Optimistic,
        Read,
        Write
    };

    /**
     * There are several ways to lock a page
     * - Exclusive (LockMode::Write)
     *      Taken by writers, so only one writer is allowed in a critical section
     * - Shared (LockMode::Read)
     *      Taken by readers usually in case of high contention where a thread
     *      tried several times (MAX_OPTIMISTIC_TRIES) to optimistically read a node.
     * - Optimistic (LockMode::Optimistic)
     *      Only taken by readers or writers while they are descending down the tree.
     *      Does not aquire any locks just reads a version field and hopes that version doesnt change.
     *      In case of reading a version while someone is holding an exclusive lock or at the end of
     *      the node scan it reads some different version then it increments tl_tries and tries again.
     *      If it fails to read optimistically for couple of runs then there is a fallback to SharedLock.
     * - None (LockMode::None)
     *      No locks taken at all. Noop.
     *      Useful when combined w reserve page, where no locks are taken.
     */
    template <LockMode Mode>
    void Lock(Page *page)
    {
        if constexpr (Mode == LockMode::Write)
            page->WDataLock();
        else if constexpr (Mode == LockMode::Read)
            page->RDataLock();
        else if constexpr (Mode == LockMode::Optimistic)
        {
            u64 ver = 0;
            while (TlState::GetTries() < MAX_OPTIMISTIC_TRIES && !page->ReadVersion(ver))
            {
                TlState::IncrementTries();
            }
            TlState::SetVersion(ver);

            if (TlState::GetTries() >= MAX_OPTIMISTIC_TRIES)
            {
                page->RDataLock();
            }
        }
        else if constexpr (Mode == LockMode::None)
            return;
        else
            DB7_UNREACHABLE();
    }

    template <LockMode Mode>
    bool Unlock(Page *page)
    {
        if constexpr (Mode == LockMode::Write)
            page->WDataUnlock();
        else if constexpr (Mode == LockMode::Read)
            page->RDataUnlock();
        else if constexpr (Mode == LockMode::Optimistic)
        {
            if (TlState::GetTries() >= MAX_OPTIMISTIC_TRIES)
            {
                page->RDataUnlock();
                return true;
            }

            u64 ver = TlState::GetVersion();
            if (!page->ValidateVersion(ver))
            {
                TlState::IncrementTries();
                return false;
            }
        }
        else if constexpr (Mode == LockMode::None)
            return true;
        else
            DB7_UNREACHABLE();

        return true;
    }
}