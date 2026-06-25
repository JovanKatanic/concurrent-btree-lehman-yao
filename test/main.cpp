#include "test_common.hpp"

int main()
{
    test_single_thread();
    test_concurrent_disjoint();
    test_concurrent_shared();

    test_single_thread_fixed();
    test_concurrent_disjoint_fixed();
    test_concurrent_shared_fixed();
}