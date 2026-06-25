// -----------------------------------------------------------------------------
// Single-threaded correctness for the FIXED (numeric) layout.
//
// BTreeIndex selects BtreeNumberLayoutLeaf whenever KeyTyp != Key, so a
// fixed-layout tree is simply BTreeIndex<u64, u64> with integer keys — no
// KeySet / encoding needed. These mirror the varlen single-thread tests.
//
// Keys are 1..N (avoiding 0 in case it's a sentinel anywhere) and inserted in
// SHUFFLED order: for integer keys, ascending insertion is the degenerate
// sorted case, so we shuffle to actually exercise splits. Values are tied to
// the key (val = 100 + key) so verification is order-independent.
//
// If the numeric layout reserves a particular key/value as a sentinel, or
// doesn't reject duplicate inserts, adjust the affected test below.
// -----------------------------------------------------------------------------
#include "test_common.hpp"

#include <algorithm>
#include <random>
#include <vector>

using namespace db7;
using namespace db7::test;

// 1..N shuffled with a fixed seed (deterministic across runs).
static std::vector<u64> make_shuffled_keys(u32 n, u32 seed = 1234)
{
    std::vector<u64> keys(n);
    for (u32 i = 0; i < n; i++)
        keys[i] = u64(i) + 1;
    std::shuffle(keys.begin(), keys.end(), std::mt19937{seed});
    return keys;
}

// insert -> present with correct value -> delete -> absent.
static void test_insert_get_delete()
{
    PagePool pool(80000);
    BTreeIndex<u64, u64> bt(&pool);
    auto keys = make_shuffled_keys(500'000);

    for (u64 k : keys)
    {
        auto result = bt.Insert(k, 100 + k);
        DB7_CHECK(result.success, "failed to insert");
    }
    for (u64 k : keys)
    {
        auto v = bt.Get(k);
        DB7_CHECK(v.success, "key should be present after insert");
        DB7_CHECK(v.value == 100 + k, "value mismatch after insert");
    }
    for (u64 k : keys)
    {
        auto result = bt.Delete(k);
        DB7_CHECK(result.success, "failed to delete");
    }
    for (u64 k : keys)
    {
        auto v = bt.Get(k);
        DB7_CHECK(!v.success, "key should be absent after delete");
    }
}

// Get on a never-inserted key must miss; insert only even keys.
static void test_missing_key()
{
    PagePool pool(80000);
    BTreeIndex<u64, u64> bt(&pool);
    auto keys = make_shuffled_keys(500'000);

    for (u64 k : keys)
        if (k % 2 == 0)
        {
            auto result = bt.Insert(k, 100 + k);
            DB7_CHECK(result.success, "failed to insert");
        }

    for (u64 k : keys)
    {
        auto v = bt.Get(k);
        if (k % 2 == 0)
        {
            DB7_CHECK(v.success, "even key should be present");
            DB7_CHECK(v.value == 100 + k, "key value inconsistent");
        }
        else
        {
            DB7_CHECK(!v.success, "odd key should be absent");
        }
    }
}

// Insert everything, delete half, confirm survivors keep their values and the
// deleted ones are gone. Exercises splits and merges.
static void test_partial_delete()
{
    const u32 N = 500'000;
    PagePool pool(80000); // <-- adjust if your tree needs more pages for N
    BTreeIndex<u64, u64> bt(&pool);
    auto keys = make_shuffled_keys(N);

    for (u64 k : keys)
    {
        auto result = bt.Insert(k, 100 + k);
        DB7_CHECK(result.success, "failed to insert");
    }
    for (u64 k : keys)
        if (k % 2 == 0)
        {
            auto result = bt.Delete(k); // delete evens
            DB7_CHECK(result.success, "failed to delete");
        }

    for (u64 k : keys)
    {
        auto v = bt.Get(k);
        if (k % 2 == 0)
        {
            DB7_CHECK(!v.success, "deleted even should be absent");
        }
        else
        {
            DB7_CHECK(v.success, "odd should survive");
            DB7_CHECK(v.value == 100 + k, "odd value should be intact");
        }
    }
}

// NOTE: assumes Insert on an existing key is REJECTED (returns !success). If the
// numeric layout updates, no-ops, or inserts a duplicate instead, adjust.
static void test_insert_same_elements()
{
    PagePool pool(80000);
    BTreeIndex<u64, u64> bt(&pool);
    auto keys = make_shuffled_keys(500);

    for (u64 k : keys)
    {
        auto result = bt.Insert(k, 1);
        DB7_CHECK(result.success, "Failed to insert");
    }
    for (u64 k : keys)
    {
        auto result = bt.Insert(k, 2);
        DB7_CHECK(!result.success, "duplicate insert should be rejected");
    }
    for (u64 k : keys)
    {
        auto v = bt.Get(k);
        DB7_CHECK(v.success, "should survive");
        DB7_CHECK(v.value == 1, "value should be intact");
    }
}

int test_single_thread_fixed()
{
    test_insert_get_delete();
    test_missing_key();
    test_partial_delete();
    test_insert_same_elements();
    return report("single_thread_fixed");
}