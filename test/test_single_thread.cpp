// -----------------------------------------------------------------------------
// Single-threaded correctness. Establishes the baseline semantics the
// concurrent suites assume. If anything here fails, fix it before touching
// threads.
// -----------------------------------------------------------------------------
#include "test_common.hpp"

using namespace db7;
using namespace db7::test;

// insert -> present with correct value -> delete -> absent  (your bench's
// semantics, but checking the value too).
static void test_insert_get_delete()
{
    PagePool pool(80000);
    BTreeIndex<Key, u64> bt(&pool);
    KeySet ks(500'000);

    for (u32 i = 0; i < ks.size(); i++)
    {
        auto result = bt.Insert(ks[i], 100 + i);
        DB7_CHECK(result.success, "failed to insert");
    }
    for (u32 i = 0; i < ks.size(); i++)
    {
        auto v = bt.Get(ks[i]);
        DB7_CHECK(v.success, "key should be present after insert");
        DB7_CHECK(v.value == 100 + i, "value mismatch after insert");
    }
    for (u32 i = 0; i < ks.size(); i++)
    {
        auto result = bt.Delete(ks[i]);
        DB7_CHECK(result.success, "failed to delete");
    }
    for (u32 i = 0; i < ks.size(); i++)
    {
        auto v = bt.Get(ks[i]);
        DB7_CHECK(!v.success, "key should be absent after delete");
    }
}

// Get on a never-inserted key must miss; interleave present/absent keys.
static void test_missing_key()
{
    PagePool pool(80000);
    BTreeIndex<Key, u64> bt(&pool);
    KeySet ks(500'000);

    for (u32 i = 0; i < ks.size(); i += 2)
    {
        auto result = bt.Insert(ks[i], i);
        DB7_CHECK(result.success, "failed to insert");
    }
    for (u32 i = 0; i < ks.size(); i++)
    {
        auto v = bt.Get(ks[i]);
        if (i % 2 == 0)
        {
            DB7_CHECK(v.success, "even key should be present");
            DB7_CHECK(v.value == i, "key value inconsitent");
        }
        else
        {
            DB7_CHECK(!v.success, "odd key should be absent");
        }
    }
}

// Larger run: insert everything, delete half, confirm the survivors keep their
// values and the deleted ones are gone. Exercises splits and merges.
static void test_partial_delete()
{
    const u32 N = 500'000;
    PagePool pool(80000); // <-- adjust if your tree needs more pages for N
    BTreeIndex<Key, u64> bt(&pool);
    KeySet ks(N);

    for (u32 i = 0; i < N; i++)
    {
        auto result = bt.Insert(ks[i], i);
        DB7_CHECK(result.success, "failed to insert");
    }
    for (u32 i = 0; i < N; i += 2)
    {
        auto result = bt.Delete(ks[i]); // delete evens
        DB7_CHECK(result.success, "failed to delete");
    }
    for (u32 i = 0; i < N; i++)
    {
        auto v = bt.Get(ks[i]);
        if (i % 2 == 0)
        {
            DB7_CHECK(!v.success, "deleted even should be absent");
        }
        else
        {
            DB7_CHECK(v.success, "odd should survive");
            DB7_CHECK(v.value == i, "odd value should be intact");
        }
    }
}

// NOTE: only valid if Insert on an existing key UPDATES the value. If your tree
// rejects duplicates, inserts a second copy, or no-ops instead, change or drop
// this test to match the real contract.
static void test_insert_same_elements()
{
    PagePool pool(80000);
    BTreeIndex<Key, u64> bt(&pool);
    KeySet ks(500);

    for (u32 i = 0; i < ks.size(); i++)
    {
        auto result = bt.Insert(ks[i], 1);
        DB7_CHECK(result.success, "Failed to insert");
    }
    for (u32 i = 0; i < ks.size(); i++)
    {
        auto result = bt.Insert(ks[i], 2);
        DB7_CHECK(!result.success, "Should throw double key exception");
    }
    for (u32 i = 0; i < ks.size(); i++)
    {
        auto v = bt.Get(ks[i]);
        DB7_CHECK(v.success, "should survive");
        DB7_CHECK(v.value == 1, "value should be intact");
    }
}

int test_single_thread()
{
    time_section("insert_get_delete", test_insert_get_delete);
    time_section("missing_key", test_missing_key);
    time_section("partial_delete", test_partial_delete);
    time_section("insert_same_elements", test_insert_same_elements);
    return report("single_thread_fixed");
}
