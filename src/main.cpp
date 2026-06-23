#include "common.hpp"
#include "btree/btree.hpp"

#include <fmt/core.h>
#include <cstring>
#include <vector>
#include <span>

using namespace db7;

int main()
{
    fmt::print("Hello, {}!\n", "world");

    u32 n = 1;

    std::vector<Key> strs(n);
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

    PagePool pool(200);

    auto btree = BTreeIndex<Key, u64>(&pool);

    for (u32 i = 0; i < n; i++)
    {
        btree.Insert(strs[i], 5);
        fmt::print("{}", btree.Get(strs[i]));
    }

    return 0;
}