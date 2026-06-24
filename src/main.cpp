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

template <typename T>
void prep_keys(std::vector<T> &strs, u32 n)
{
    if constexpr (std::is_same_v<T, Key>)
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
    else
    {
        for (T i = 0; i < n; i++)
        {
            strs[i] = i;
        }
    }
}

int main()
{
    fmt::print("Hello, {}!\n", "world");

    u32 n = 1'000'000;

    using typ = Key;

    std::vector<typ> strs(n);
    prep_keys(strs, n);
    std::shuffle(strs.begin(), strs.end(), std::mt19937{42});

    auto bench = [&](int reps)
    {
        double best = std::numeric_limits<double>::max();
        for (int r = 0; r < reps; r++)
        {
            PagePool pool(80000);

            auto btree = BTreeIndex<typ, u64>(&pool);

            auto t0 = std::chrono::steady_clock::now();
            for (u32 i = 0; i < n; i++)
            {
                btree.Insert(strs[i], 5);
                auto v = btree.Get(strs[i]);
                if (v != 5)
                    throw std::runtime_error("bad");
            }
            auto t1 = std::chrono::steady_clock::now();

            double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
            if (ns < best)
                best = ns;
        }
        printf("best: %.2f ms  (%.1f ns/op)\n", best / 1e6, best / n);
    };

    bench(10);

    return 0;
}