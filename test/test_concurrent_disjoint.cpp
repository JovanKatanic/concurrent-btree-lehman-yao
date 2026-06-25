// -----------------------------------------------------------------------------
// Concurrent, DISJOINT key partitions: each thread owns a contiguous slice of
// the key space, so no two threads ever touch the same key. The final state is
// therefore fully deterministic and can be verified exactly afterwards.
//
// This is the cornerstone concurrency test: it stresses shared structure
// (node splits/merges on adjacent leaves, root growth) without needing any
// per-key ordering guarantee. Start here. Run it under TSan.
//
// If THIS fails or races, your tree's internal latching is the problem.
// -----------------------------------------------------------------------------
#include "test_common.hpp"

using namespace db7;
using namespace db7::test;

int test_concurrent_disjoint()
{
    const u32 N = 500'000;
    const unsigned T = default_threads();

    PagePool pool(80'000); // <-- size generously; tune to your page-per-key math
    BTreeIndex<Key, u64> bt(&pool);
    KeySet ks(N);

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
           auto res =  bt.Insert(ks[i], 1000 + i);
           DB7_CHECK(res.success, "Failed to insert");
        } });
    for (u32 i = 0; i < N; i++)
    {
        auto v = bt.Get(ks[i]);
        DB7_CHECK(v.success, "all keys present after concurrent insert");
        DB7_CHECK(v.value == 1000 + i, "value intact after concurrent insert");
    }

    // Phase 2: concurrent disjoint deletes (each thread deletes evens in its
    // own slice).
    double del_ns = run_parallel(T, [&](unsigned t)
                                 {
        u32 lo, hi;
        partition(t, lo, hi);
        for (u32 i = lo; i < hi; i++)
            if (i % 2 == 0){
                auto res = bt.Delete(ks[i]); 
                DB7_CHECK(res.success, "Failed to delete");
            } });

    double read_ns = run_parallel(T, [&](unsigned t)
                                  {
        u32 lo, hi;
        partition(t, lo, hi);
        for (u32 i = lo; i < hi; i++)
        {
            auto v = bt.Get(ks[i]);
            if (i % 2 == 0)
            {
                DB7_CHECK(!v.success, "even key deleted");
            }
            else
            {
                DB7_CHECK(v.success, "odd key survives");
                DB7_CHECK(v.value == 1000 + i, "odd value intact");
            }
        } });

    std::fprintf(stderr, "threads=%u  insert=%.1f ms  delete=%.1f ms read=%.1f ms\n", T,
                 ins_ns / 1e6, del_ns / 1e6, read_ns / 1e6);
    return report("concurrent_disjoint");
}
