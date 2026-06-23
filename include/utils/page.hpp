#pragma once

#include "common.hpp"
#include "utils/adaptive_version_lock.hpp"

#include <atomic>

namespace db7
{
    class alignas(CACHE_LINE_SIZE) Page
    {
    private:
        AdaptiveVersionLock lock_;
        byte *data_;
        page_id pid_;
        std::atomic<u32> ref_count_;

    public:
        Page() : data_(nullptr), pid_(0), ref_count_(0) {}

        /**
         * Data
         */
        byte *GetData() const { return data_; }

        void SetData(byte *data) { data_ = data; }

        /**
         * Page ID
         */
        page_id GetId() const { return pid_; }

        void SetId(page_id id) { pid_ = id; }

        /**
         * Ref count
         */
        void Pin() { ref_count_++; }

        void Unpin() { ref_count_--; }

        /**
         * Locks
         */
        void RDataLock() { lock_.ReadLock(); }

        void RDataUnlock() { lock_.ReadUnlock(); }

        void WDataLock() { lock_.WriteLock(); }

        void WDataUnlock() { lock_.WriteUnlock(); }

        bool ReadVersion(u64 &version) { return lock_.ReadOptimistic(version); }

        bool ValidateVersion(u64 version) { return lock_.Validate(version); }
    };

    // TODO static_assert(sizeof(Page) <= CACHE_LINE_SIZE, "page spans 2 cache lines");
}