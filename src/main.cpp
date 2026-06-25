#include "common.hpp"
#include "btree/btree.hpp"

#include <fmt/core.h>
#include <cstring>
#include <vector>
#include <span>
#include <random>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <thread>

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

template <typename Fn>
inline double run_parallel(unsigned nthreads, Fn &&fn)
{
    std::atomic<unsigned> ready{0};
    std::atomic<bool> go{false};
    std::vector<std::thread> ts;
    ts.reserve(nthreads);

    for (unsigned t = 0; t < nthreads; t++)
    {
        ts.emplace_back([&, t]
                        {
            ready.fetch_add(1, std::memory_order_acq_rel);
            while (!go.load(std::memory_order_acquire)) { /* spin */
            }
            fn(t); });
    }
    while (ready.load(std::memory_order_acquire) < nthreads)
    { /* spin */
    }
    auto t0 = std::chrono::steady_clock::now();
    go.store(true, std::memory_order_release);
    for (auto &th : ts)
        th.join();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count();
}

inline unsigned default_threads()
{
    unsigned n = std::thread::hardware_concurrency();
    return n ? n : 4;
}

int main()
{
    fmt::print("Hello, {}!\n", "world");

    u32 n = 1'000'000;
    u32 PREFILL = 100'000;

    using typ = Key;

    auto T = default_threads();

    // auto partition = [&](unsigned t, u32 &lo, u32 &hi)
    // {
    //     u32 per = (n + T - 1) / T;
    //     lo = t * per;
    //     hi = std::min<u32>(lo + per, n);
    // };
    auto partition = [&](unsigned t, u32 &lo, u32 &hi)
    {
        u32 measured = n - PREFILL;
        u32 per = (measured + T - 1) / T;
        lo = PREFILL + t * per;
        hi = std::min<u32>(lo + per, n);
    };

    PagePool pool(100'000);

    auto btree = BTreeIndex<typ, u64>(&pool);

    std::vector<typ> strs(n);
    prep_keys(strs, n);
    std::shuffle(strs.begin(), strs.end(), std::mt19937{42});

    for (u32 i = 0; i < PREFILL; i++)
    {
        auto res = btree.Insert(strs[i], 1000 + i);
        DB7_ASSERT(res.success, "Failed to insert");
    }

    // Phase 1: concurrent disjoint inserts.
    double ins_ns = run_parallel(T, [&](unsigned t)
                                 {
        u32 lo, hi;
        partition(t, lo, hi);
        for (u32 i = lo; i < hi; i++)
        {
           auto res =  btree.Insert(strs[i], 1000 + i);
           DB7_ASSERT(res.success, "Failed to insert");
        } });

    double read_ns = run_parallel(T, [&](unsigned t)
                                  {
        u32 lo, hi;
        partition(t, lo, hi);
        for (u32 i = lo; i < hi; i++)
        {
           auto v = btree.Get(strs[i]);
            DB7_ASSERT(v.success, "all keys present after concurrent insert");
            DB7_ASSERT(v.value == 1000 + i, "value intact after concurrent insert");
        } });

    std::fprintf(stderr, "threads=%u  insert=%.1f ms  read=%.1f ms\n", T,
                 ins_ns / 1e6, read_ns / 1e6);

    // auto bench = [&](int reps)
    // {
    //     double best = std::numeric_limits<double>::max();
    //     for (int r = 0; r < reps; r++)
    //     {
    //         PagePool pool(80000);

    //         auto btree = BTreeIndex<typ, u64>(&pool);

    //         auto t0 = std::chrono::steady_clock::now();
    //         for (u32 i = 0; i < n; i++)
    //         {
    //             btree.Insert(strs[i], 5);
    //             // btree.Delete(strs[i]);//v.success != false
    //             auto v = btree.Get(strs[i]);
    //             if (v.value != 5)
    //                 throw std::runtime_error("bad");
    //         }
    //         auto t1 = std::chrono::steady_clock::now();

    //         double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    //         if (ns < best)
    //             best = ns;
    //     }
    //     printf("best: %.2f ms  (%.1f ns/op)\n", best / 1e6, best / n);
    // };

    // bench(10);

    return 0;
}