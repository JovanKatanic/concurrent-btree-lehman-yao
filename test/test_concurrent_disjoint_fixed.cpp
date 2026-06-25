// -----------------------------------------------------------------------------
// FIXED (numeric) layout — concurrent, DISJOINT key partitions. Mirror of the
// varlen disjoint test: each thread owns a contiguous slice of the shuffled key
// array, so no two threads touch the same key and the final state is exactly
// verifiable.
//
// Keys are shuffled once up front: partitioning raw 1..N by index would give
// each thread an ascending (sorted) run, the degenerate insert path. Shuffling
// keeps partitions disjoint while scattering keys across the tree. Values are
// tied to the key (val = 1000 + key) so checks are order-independent.
//
// Run under TSan. If this races, the latching is the problem.
// -----------------------------------------------------------------------------
#include "test_common.hpp"

#include <algorithm>
#include <random>
#include <vector>

using namespace db7;
using namespace db7::test;

int test_concurrent_disjoint_fixed()
{
    const u32 N = 500'000;
    const unsigned T = default_threads();

    PagePool pool(80'000); // <-- size to your page-per-key math
    BTreeIndex<u64, u64> bt(&pool);

    // keys 1..N, shuffled once (deterministic seed)
    std::vector<u64> keys(N);
    for (u32 i = 0; i < N; i++) keys[i] = u64(i) + 1;
    std::shuffle(keys.begin(), keys.end(), std::mt19937{1234});

    auto partition = [&](unsigned t, u32 &lo, u32 &hi)
    {
        u32 per = (N + T - 1) / T;
        lo = t * per;
        hi = std::min<u32>(lo + per, N);
    };

    // Phase 1: concurrent disjoint inserts.
    double ins_ns = run_parallel(T, [&](unsigned t)
                                 {
        u32 lo, hi;
        partition(t, lo, hi);
        for (u32 i = lo; i < hi; i++)
        {
            auto res = bt.Insert(keys[i], 1000 + keys[i]);
            DB7_CHECK(res.success, "Failed to insert");
        } });
    for (u32 i = 0; i < N; i++)
    {
        auto v = bt.Get(keys[i]);
        DB7_CHECK(v.success, "all keys present after concurrent insert");
        DB7_CHECK(v.value == 1000 + keys[i], "value intact after concurrent insert");
    }

    // Phase 2: concurrent disjoint deletes (each thread deletes even keys in its
    // own slice).
    double del_ns = run_parallel(T, [&](unsigned t)
                                 {
        u32 lo, hi;
        partition(t, lo, hi);
        for (u32 i = lo; i < hi; i++)
            if (keys[i] % 2 == 0) {
                auto res = bt.Delete(keys[i]);
                DB7_CHECK(res.success, "Failed to delete");
            } });

    double read_ns = run_parallel(T, [&](unsigned t)
                                  {
        u32 lo, hi;
        partition(t, lo, hi);
        for (u32 i = lo; i < hi; i++)
        {
            auto v = bt.Get(keys[i]);
            if (keys[i] % 2 == 0)
            {
                DB7_CHECK(!v.success, "even key deleted");
            }
            else
            {
                DB7_CHECK(v.success, "odd key survives");
                DB7_CHECK(v.value == 1000 + keys[i], "odd value intact");
            }
        } });

    std::fprintf(stderr,
                 "[concurrent_disjoint_fixed] threads=%u  insert=%.1f ms  delete=%.1f ms  read=%.1f ms\n",
                 T, ins_ns / 1e6, del_ns / 1e6, read_ns / 1e6);
    return report("concurrent_disjoint_fixed");
}
