#pragma once
// -----------------------------------------------------------------------------
// Shared scaffolding for the BTreeIndex test suites.
//
// ADAPT THESE TO YOUR REAL API IF THEY DIFFER:
//   * Key field names (data / len / encoded / enc_len)
//   * KeyNormEncoder::Encode signature
//   * the shape of BTreeIndex::Get(...)'s return value (.success / .value)
//   * PagePool sizing (the page-per-key ratio is unknown to me — see notes)
//
// Key generation runs ONCE, single-threaded, up front. The resulting Key
// objects are immutable afterwards, so sharing them across threads by const
// reference is safe.
// -----------------------------------------------------------------------------
#include "common.hpp"
#include "btree/btree.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>
#include <random>

namespace db7::test
{

    // ---- tiny assertion harness -------------------------------------------------
    struct Counters
    {
        std::atomic<u64> checks{0};
        std::atomic<u64> failures{0};
    };
    inline Counters g_counters;

#define DB7_CHECK(cond, msg)                                                    \
    do                                                                          \
    {                                                                           \
        ::db7::test::g_counters.checks.fetch_add(1, std::memory_order_relaxed); \
        if (!(cond))                                                            \
        {                                                                       \
            ::db7::test::g_counters.failures.fetch_add(                         \
                1, std::memory_order_relaxed);                                  \
            std::fprintf(stderr, "[FAIL] %s:%d  %s\n", __FILE__, __LINE__,      \
                         (msg));                                                \
        }                                                                       \
    } while (0)

    inline int report(const char *suite)
    {
        u64 c = g_counters.checks.load();
        u64 f = g_counters.failures.load();
        std::fprintf(stderr, "[%s] %llu checks, %llu failures\n", suite,
                     (unsigned long long)c, (unsigned long long)f);
        return f == 0 ? 0 : 1;
    }

    // ---- key generation with owned storage --------------------------------------
    // Owns every buffer it allocates so ASan/LSan stay quiet and the Key objects
    // stay valid for the whole test. Same logic as your prep_keys(), just with a
    // destructor.
    struct KeySet
    {
        std::vector<Key> keys;
        std::vector<byte *> owned; // freed in dtor

        explicit KeySet(u32 n)
        {
            keys.resize(n);
            owned.reserve(static_cast<size_t>(n) * 2);
            for (u32 i = 0; i < n; i++)
            {
                std::string s = "kEY_ⅶ_⎞_Љ_۝_" + std::to_string(i + 1);
                byte *raw = new byte[s.size()];
                byte *buf = new byte[s.size() * 16];
                owned.push_back(raw);
                owned.push_back(buf);

                std::memcpy(raw, s.data(), s.size());
                std::span<const byte> sp((const byte *)s.data(), (u16)s.size());
                u32 len = KeyNormEncoder::Encode(buf, sp, false, false, false);

                keys[i].data = raw;
                keys[i].len = (u16)s.size();
                keys[i].encoded = buf;
                keys[i].enc_len = (u16)len;
            }
            std::shuffle(keys.begin(), keys.end(), std::mt19937{42});
        }
        ~KeySet()
        {
            for (byte *p : owned)
                delete[] p;
        }

        KeySet(const KeySet &) = delete;
        KeySet &operator=(const KeySet &) = delete;

        u32 size() const { return (u32)keys.size(); }
        const Key &operator[](u32 i) const { return keys[i]; }
    };

    // numeric-key variant, in case you also instantiate BTreeIndex<u64, u64>
    inline std::vector<u64> make_int_keys(u32 n)
    {
        std::vector<u64> v(n);
        for (u32 i = 0; i < n; i++)
            v[i] = i;
        std::shuffle(v.begin(), v.end(), std::mt19937{42});
        return v;
    }

    // ---- parallel runner --------------------------------------------------------
    // Spawns `nthreads`, holds them at a spin barrier, then releases them all at
    // once so contention starts simultaneously. Returns wall-clock ns of the
    // parallel region.
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
} // namespace db7::test

int test_single_thread();
int test_concurrent_disjoint();
int test_concurrent_shared();
