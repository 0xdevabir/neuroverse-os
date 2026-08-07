// tests/unit/pkg/digest_test.cpp
//
// Tests for neuro::pkg::DigestN<N> — fixed-size hash digest wrappers.
// Covers:
//
//   - alias sizes (Digest224/256/384/512/Digest)
//   - default-constructed digest is zero-filled
//   - indexing / data() / size() round-trip
//   - equality / inequality
//   - lexicographic less-than (used as std::map key)
//   - to_hex emits lowercase hex of correct length
//   - to_hex of zeros is the canonical "0…0" string
//   - to_hex pads single-nibble values

#include "neuro/pkg/digest.hpp"

#include <cstdint>
#include <map>
#include <set>

#include "../../test_framework.hpp"

using neuro::pkg::Digest;
using neuro::pkg::Digest224;
using neuro::pkg::Digest256;
using neuro::pkg::Digest384;
using neuro::pkg::Digest512;
using neuro::pkg::DigestN;
using neuro::pkg::to_hex;

// ---- 1. alias sizes -------------------------------------------------

TEST(digest, alias_sizes) {
    EXPECT_EQ(static_cast<std::size_t>(28), Digest224{}.size());
    EXPECT_EQ(static_cast<std::size_t>(32), Digest256{}.size());
    EXPECT_EQ(static_cast<std::size_t>(48), Digest384{}.size());
    EXPECT_EQ(static_cast<std::size_t>(64), Digest512{}.size());
    EXPECT_EQ(static_cast<std::size_t>(64), Digest{}.size());
}

// ---- 2. default ctor is zero-filled --------------------------------

TEST(digest, default_zero_filled) {
    Digest256 d{};
    for (std::size_t i = 0; i < d.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(0), d[i]);
    }
    Digest d2{};
    for (std::size_t i = 0; i < d2.size(); ++i) {
        EXPECT_EQ(static_cast<std::uint8_t>(0), d2[i]);
    }
}

// ---- 3. indexing / data round-trip ---------------------------------

TEST(digest, index_and_data) {
    Digest256 d{};
    d[0]  = 0xDE;
    d[31] = 0xAD;
    EXPECT_EQ(static_cast<std::uint8_t>(0xDE), d[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xAD), d[31]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xDE), d.data()[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xAD), d.data()[31]);
    EXPECT_EQ(d.data(), const_cast<const Digest256&>(d).data());
}

// ---- 4. equality / inequality --------------------------------------

TEST(digest, equality) {
    Digest256 a{};
    Digest256 b{};
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    b[5] = 0x01;
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// ---- 5. different-size digests are not interchangeable -------------

TEST(digest, size_mismatch_is_distinct_type) {
    Digest256 a{};
    Digest512 b{};
    EXPECT_EQ(static_cast<std::size_t>(32), a.size());
    EXPECT_EQ(static_cast<std::size_t>(64), b.size());
    static_assert(!std::is_same_v<Digest256, Digest512>);
}

// ---- 6. lexicographic less-than (map key usability) ---------------

TEST(digest, less_than_is_lexicographic) {
    Digest256 a{};
    Digest256 b{};
    EXPECT_FALSE(a < b);

    a[0] = 0x01;
    EXPECT_FALSE(a < b);   // a[0]=1 > b[0]=0
    EXPECT_TRUE(b < a);

    b[0] = 0x01;
    EXPECT_FALSE(a < b);

    b[0] = 0x02;
    EXPECT_TRUE(a < b);    // a[0]=1 < b[0]=2

    // Tie-break on later byte.
    Digest256 c{};
    Digest256 d{};
    for (std::size_t i = 0; i < 32; ++i) {
        c[i] = static_cast<std::uint8_t>(i);
        d[i] = static_cast<std::uint8_t>(i);
    }
    c[10] = 0x00;
    d[10] = 0xFF;
    EXPECT_TRUE(c < d);
}

// ---- 7. usable as map key -----------------------------------------

TEST(digest, usable_as_map_key) {
    std::map<Digest256, int> m;
    Digest256 k1{};
    Digest256 k2{};
    k1[0] = 0x01;
    k2[0] = 0x02;
    m[k1] = 100;
    m[k2] = 200;
    EXPECT_EQ(2u, m.size());
    EXPECT_EQ(100, m[k1]);
    EXPECT_EQ(200, m[k2]);

    std::set<Digest> s;
    Digest d{};
    s.insert(d);
    EXPECT_EQ(1u, s.count(d));
}

// ---- 8. to_hex emits lowercase hex of correct length --------------

TEST(digest, to_hex_length) {
    Digest224 d{};
    EXPECT_EQ(static_cast<std::size_t>(56), to_hex(d).size());
    Digest256 d2{};
    EXPECT_EQ(static_cast<std::size_t>(64), to_hex(d2).size());
    Digest512 d3{};
    EXPECT_EQ(static_cast<std::size_t>(128), to_hex(d3).size());
}

TEST(digest, to_hex_zero_is_all_zeros) {
    Digest256 d{};
    std::string h = to_hex(d);
    for (char c : h) EXPECT_EQ('0', c);
}

TEST(digest, to_hex_lowercase) {
    Digest256 d{};
    d[0] = 0xAB;
    d[1] = 0xCD;
    auto h = to_hex(d);
    EXPECT_EQ("abcd", h.substr(0, 4));
}

// ---- 9. to_hex pads single nibble ---------------------------------

TEST(digest, to_hex_pads_single_nibble) {
    Digest256 d{};
    d[0] = 0x0F;
    auto h = to_hex(d);
    EXPECT_EQ("0f", h.substr(0, 2));

    d[0] = 0xF0;
    EXPECT_EQ("f0", to_hex(d).substr(0, 2));

    d[0] = 0x00;
    EXPECT_EQ("00", to_hex(d).substr(0, 2));
}

// ---- 10. round-trip to_hex / index ---------------------------------

TEST(digest, hex_round_trip) {
    Digest256 d{};
    for (std::size_t i = 0; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(i * 7 + 3);
    }
    auto h = to_hex(d);
    EXPECT_EQ(static_cast<std::size_t>(64), h.size());
    for (std::size_t i = 0; i < d.size(); ++i) {
        std::string byte_hex;
        byte_hex += "0123456789abcdef"[(d[i] >> 4) & 0xF];
        byte_hex += "0123456789abcdef"[d[i] & 0xF];
        EXPECT_EQ(byte_hex, h.substr(i * 2, 2));
    }
}

RUN_ALL_TESTS()
