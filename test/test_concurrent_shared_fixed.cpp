// -----------------------------------------------------------------------------
// FIXED (numeric) layout — concurrent SHARED key space. Mirror of the varlen
// shared test: all threads contend on the same keys, so these are race/crash
// finders best run under ThreadSanitizer.
//
//   1) insert storm -- every thread inserts the whole set with the SAME value
//      per key, so the final state is deterministic regardless of who wins.
//   2) random chaos -- mixed insert/(delete)/get; only invariant asserted is
//      that a hit returns a value we actually wrote. TSan/ASan do the rest.
//
// Integer keys, so no KeySet — keys are just 1..N.
// -----------------------------------------------------------------------------
#include "test_common.hpp"

#include <random>
#include <vector>

using namespace db7;
using namespace db7::test;

// All threads race to insert the entire key set with the same value per key.
static void shared_insert_storm(unsigned T)
{
    const u32 N = 50'000;
    PagePool pool(100'000);
    BTreeIndex<u64, u64> bt(&pool);

    run_parallel(T, [&](unsigned)
                 {
        for (u32 i = 0; i < N; i++) bt.Insert(u64(i) + 1, 7); });

    for (u32 i = 0; i < N; i++)
    {
        auto v = bt.Get(u64(i) + 1);
        DB7_CHECK(v.success, "key present after insert storm");
        DB7_CHECK(v.value == 7, "value equals the single written value");
    }
}

// Random mixed operations on a shared space. State is non-deterministic; the
// only thing we can assert is that any successful Get returns a written value.
static void shared_chaos(unsigned T)
{
    const u32 N = 20'000;
    PagePool pool(1'000'000);
    BTreeIndex<u64, u64> bt(&pool);
    const int OPS_PER_THREAD = 200'000;
    const u64 LEGAL_VALUE = 42;

    run_parallel(T, [&](unsigned t)
                 {
        std::mt19937 rng(1234u + t);
        std::uniform_int_distribution<u32> pick(1, N);   // keys 1..N
        std::uniform_int_distribution<int> op(0, 2);
        for (int n = 0; n < OPS_PER_THREAD; n++) {
            u64 k = pick(rng);
            switch (op(rng)) {
                case 0:
                    bt.Insert(k, LEGAL_VALUE);
                    break;
                case 1:
                    bt.Delete(k);
                    break;
                default: {
                    auto v = bt.Get(k);
                    if (v.success)
                        DB7_CHECK(v.value == LEGAL_VALUE,
                                  "hit must return a legal (written) value");
                }
            }
        } });
}

int test_concurrent_shared_fixed()
{
    const unsigned T = default_threads();
    shared_insert_storm(T);
    shared_chaos(T);
    return report("concurrent_shared_fixed");
}
