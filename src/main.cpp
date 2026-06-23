#include "common.hpp"
#include "btree/btree.hpp"

#include <fmt/core.h>
#include <cstring>
#include <vector>
#include <span>
#include <random>

using namespace db7;

static inline u64 now_ns()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return u64(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
}

void prep_keys(std::vector<Key> &strs, u32 n)
{
    for (u32 i = 0; i < n; i++)
    {
        std::string s = "kEY_ⅶ_⎞_Љ_۝_" + std::to_string(i + 1);
        byte *buf = new byte[s.size() * 16];
        byte *raw = new byte[s.size()]; // ← own copy of raw too

        std::memcpy(raw, s.data(), s.size());

        std::span sp((const byte *)s.data(), (u16)s.size());
        u32 len = KeyNormEncoder::Encode(buf, sp, false, false, false);

        strs[i].data = raw;
        strs[i].len = (u16)s.size();
        strs[i].encoded = buf;
        strs[i].enc_len = (u16)(len);
    }
}

int main()
{
    fmt::print("Hello, {}!\n", "world");

    u32 n = 1'000'000;

    using typ = Key;

    // std::vector<typ> strs(n);
    // for (u32 i = 0; i < n; i++)
    //     strs[i] = i;

    std::vector<Key> strs(n);
    prep_keys(strs, n);
    std::shuffle(strs.begin(), strs.end(), std::mt19937{42});

    PagePool pool(80000);

    auto btree = BTreeIndex<typ, u64>(&pool);

    u64 t0 = now_ns();

    for (u32 i = 0; i < n; i++)
    {
        btree.Insert(strs[i], 5);
        if (5 != btree.Get(strs[i]))
        {
            throw std::runtime_error("yes");
        }
    }

    u64 t1 = now_ns();

    printf("insert:        %.3f ms\n", (t1 - t0) / 1e6);

    return 0;
}