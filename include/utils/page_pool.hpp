#pragma once

#include "common.hpp"
#include "utils/page.hpp"

namespace db7
{
    class PagePool
    {
    private:
        Page *pages_;
        std::atomic<u64> next_free_;

    public:
        PagePool(u32 page_count) : next_free_(0)
        {
            pages_ = new Page[page_count];
            byte *data = reinterpret_cast<byte *>(std::aligned_alloc(4096, static_cast<size_t>(page_count) * DB7_PAGE_SIZE));
            DB7_ASSERT(data != nullptr, "Failed to allocate");
            for (u32 i = 0; i < page_count; i++)
            {
                pages_[i].SetId(i);
                pages_[i].SetData(data + (i * DB7_PAGE_SIZE));
            }
        }

        ~PagePool()
        {
            delete[] pages_;
        }

        Page *Reserve() { return pages_ + (next_free_++); }

        Page *Get(u64 pid)
        {
            auto *page = pages_ + pid;
            page->Pin();
            return page;
        }

        void Return(Page *page)
        {
            page->Unpin();
        }
    };
}