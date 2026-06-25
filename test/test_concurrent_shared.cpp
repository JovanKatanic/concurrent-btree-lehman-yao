// -----------------------------------------------------------------------------
// Concurrent SHARED key space: threads contend on the same keys. Final state is
// generally non-deterministic, so these tests are mainly race/crash finders --
// run them under ThreadSanitizer, where they're most valuable.
//
// Two patterns:
//   1) insert storm  -- every thread inserts the whole set with the SAME value
//      per key, so the final state IS deterministic regardless of who wins each
//      race. Catches corruption on contended split paths.
//   2) random chaos  -- mixed insert/delete/get on random keys. We only assert
//      an invariant (a hit returns a legal value) + rely on TSan/ASan for the
//      rest.
// -----------------------------------------------------------------------------
#include "test_common.hpp"

#include <random>

using namespace db7;
using namespace db7::test;

// All threads race to insert the entire key set. Same value per key => the
// surviving state is well-defined no matter how the races resolve.
static void shared_insert_storm(unsigned T)
{
    PagePool pool(100000);
    BTreeIndex<Key, u64> bt(&pool);
    KeySet ks(50000);

    run_parallel(T, [&](unsigned)
                 {
        for (u32 i = 0; i < ks.size(); i++) bt.Insert(ks[i], 7); });

    for (u32 i = 0; i < ks.size(); i++)
    {
        auto v = bt.Get(ks[i]);
        DB7_CHECK(v.success, "key present after insert storm");
        DB7_CHECK(v.value == 7, "value equals the single written value");
    }
}

// Random mixed operations on a shared space. State is non-deterministic; the
// only thing we can assert is that any successful Get returns a value we
// actually wrote. The real bug-finder here is the sanitizer.
static void shared_chaos(unsigned T)
{
    PagePool pool(1000000);
    BTreeIndex<Key, u64> bt(&pool);
    KeySet ks(20000);
    const int OPS_PER_THREAD = 200000;
    const u64 LEGAL_VALUE = 42;

    run_parallel(T, [&](unsigned t)
                 {
        std::mt19937 rng(1234u + t);
        std::uniform_int_distribution<u32> pick(0, ks.size() - 1);
        std::uniform_int_distribution<int> op(0, 2);
        for (int n = 0; n < OPS_PER_THREAD; n++) {
            u32 i = pick(rng);
            switch (op(rng)) {
                case 0:
                    bt.Insert(ks[i], LEGAL_VALUE);
                    break;
                case 1:
                    bt.Delete(ks[i]);
                    break;
                default: {
                    auto v = bt.Get(ks[i]);
                    if (v.success)
                        DB7_CHECK(v.value == LEGAL_VALUE,
                                  "hit must return a legal (written) value");
                }
            }
        } });
}

int test_concurrent_shared()
{
    const unsigned T = default_threads();
    shared_insert_storm(T);
    shared_chaos(T);
    return report("concurrent_shared");
}
