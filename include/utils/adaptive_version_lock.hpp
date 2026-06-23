#pragma once

#include "common.hpp"

#include <atomic>
#include <shared_mutex>

namespace db7
{
    class AdaptiveVersionLock
    {
    private:
        std::atomic<u64> seq{0};
        std::shared_mutex rw_mtx;

    public:
        void WriteLock()
        {
            rw_mtx.lock();
            seq.fetch_add(1, std::memory_order_release);
        }

        void WriteUnlock()
        {
            seq.fetch_add(1, std::memory_order_release);
            rw_mtx.unlock();
        }

        void ReadLock()
        {
            rw_mtx.lock_shared();
        }

        void ReadUnlock()
        {
            rw_mtx.unlock_shared();
        }

        bool ReadOptimistic(u64 &version)
        {
            version = seq.load(std::memory_order_acquire);
            return !(version & 1);
        }

        bool Validate(u64 version)
        {
            std::atomic_thread_fence(std::memory_order_acquire);
            return seq.load() == version; // std::memory_order_relaxed
        }

        class ReadGuard
        {
            AdaptiveVersionLock &lock_;

        public:
            ReadGuard(AdaptiveVersionLock &lock) : lock_(lock) { lock_.ReadLock(); }
            ~ReadGuard() { lock_.ReadUnlock(); }
            ReadGuard(const ReadGuard &) = delete;
            ReadGuard &operator=(const ReadGuard &) = delete;
        };

        class WriteGuard
        {
            AdaptiveVersionLock &lock_;

        public:
            WriteGuard(AdaptiveVersionLock &lock) : lock_(lock) { lock_.WriteLock(); }
            ~WriteGuard() { lock_.WriteUnlock(); }
            WriteGuard(const WriteGuard &) = delete;
            WriteGuard &operator=(const WriteGuard &) = delete;
        };
    };
}