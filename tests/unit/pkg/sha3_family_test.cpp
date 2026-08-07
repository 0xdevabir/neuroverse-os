// tests/unit/pkg/sha3_family_test.cpp
//
// Tests for the full FIPS-202 SHA3 family (SHA3-224 / SHA3-256 /
// SHA3-384 / SHA3-512) plus the extendable-output functions
// (SHAKE128 / SHAKE256).
//
// All hex digests below are reproduced verbatim from NIST CAVP /
// FIPS 202 examples. If a future change regresses any of them,
// the failure catches the regression before it can land.

#include "neuro/pkg/sha3.hpp"

#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "../../test_framework.hpp"

using neuro::pkg::Digest224;
using neuro::pkg::Digest256;
using neuro::pkg::Digest384;
using neuro::pkg::Digest512;
using neuro::pkg::DigestN;
using neuro::pkg::sha3::sha3_224;
using neuro::pkg::sha3::sha3_256;
using neuro::pkg::sha3::sha3_384;
using neuro::pkg::sha3::sha3_512;
using neuro::pkg::sha3::Shake128;
using neuro::pkg::sha3::Shake256;

// ---- Hex helper ---------------------------------------------------------

template <std::size_t N>
static DigestN<N> digest_from_hex(const char* hex) {
    DigestN<N> d{};
    auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    for (std::size_t i = 0; i < N; ++i) {
        d[i] = (nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]);
    }
    return d;
}

static std::vector<std::uint8_t>
bytes_from_hex(const char* hex) {
    auto nibble = [](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    std::size_t n = std::strlen(hex) / 2;
    std::vector<std::uint8_t> out(n);
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = (nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]);
    }
    return out;
}

static std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> b(s.size());
    std::memcpy(b.data(), s.data(), s.size());
    return b;
}

// ---- SHA3-224 -----------------------------------------------------------

TEST(sha3_family, sha3_224_empty) {
    Digest224 expected = digest_from_hex<28>(
        "6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7");
    Digest224 got = sha3_224(std::span<const std::byte>{});
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, sha3_224_abc) {
    Digest224 expected = digest_from_hex<28>(
        "e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf");
    Digest224 got = sha3_224(std::string_view{"abc"});
    EXPECT_EQ(expected, got);
}

// ---- SHA3-256 -----------------------------------------------------------

TEST(sha3_family, sha3_256_empty) {
    Digest256 expected = digest_from_hex<32>(
        "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a");
    Digest256 got = sha3_256(std::span<const std::byte>{});
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, sha3_256_abc) {
    Digest256 expected = digest_from_hex<32>(
        "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532");
    Digest256 got = sha3_256(std::string_view{"abc"});
    EXPECT_EQ(expected, got);
}

// ---- SHA3-384 -----------------------------------------------------------

TEST(sha3_family, sha3_384_empty) {
    Digest384 expected = digest_from_hex<48>(
        "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2a"
        "c3713831264adb47fb6bd1e058d5f004");
    Digest384 got = sha3_384(std::span<const std::byte>{});
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, sha3_384_abc) {
    Digest384 expected = digest_from_hex<48>(
        "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b2"
        "98d88cea927ac7f539f1edf228376d25");
    Digest384 got = sha3_384(std::string_view{"abc"});
    EXPECT_EQ(expected, got);
}

// ---- SHA3-512 cross-check (regression for the canonical flavour) -------

TEST(sha3_family, sha3_512_still_correct) {
    Digest512 expected = digest_from_hex<64>(
        "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
        "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26");
    Digest512 got = sha3_512(std::span<const std::byte>{});
    EXPECT_EQ(expected, got);
}

// ---- Streaming absorb matches oneshot for every flavour ----------------

TEST(sha3_family, streaming_matches_oneshot_for_every_size) {
    std::string s(500, 'x');

    Digest224 o224 = sha3_224(std::string_view{s});
    neuro::pkg::sha3::Sha3_224 h224;
    h224.absorb(bytes_of(s));
    EXPECT_EQ(o224, h224.final());

    Digest256 o256 = sha3_256(std::string_view{s});
    neuro::pkg::sha3::Sha3_256 h256;
    h256.absorb(bytes_of(s));
    EXPECT_EQ(o256, h256.final());

    Digest384 o384 = sha3_384(std::string_view{s});
    neuro::pkg::sha3::Sha3_384 h384;
    h384.absorb(bytes_of(s));
    EXPECT_EQ(o384, h384.final());

    Digest512 o512 = sha3_512(std::string_view{s});
    neuro::pkg::sha3::Sha3_512 h512;
    h512.absorb(bytes_of(s));
    EXPECT_EQ(o512, h512.final());
}

// ---- SHAKE128 -----------------------------------------------------------

TEST(sha3_family, shake128_empty_32_bytes) {
    auto got = Shake128::oneshot(std::span<const std::byte>{}, 32);
    auto expected = bytes_from_hex(
        "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26");
    EXPECT_EQ(expected.size(), got.size());
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, shake128_abc_32_bytes) {
    auto got = Shake128::oneshot(std::string_view{"abc"}, 32);
    auto expected = bytes_from_hex(
        "5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8");
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, shake128_extendable_output_crosses_rate) {
    // SHAKE128 rate = 168 bytes. Squeeze 200 bytes to force a
    // second permutation (multi-block output).
    auto got = Shake128::oneshot(std::string_view{std::string(200, 'q')}, 200);
    auto expected = bytes_from_hex(
        "d31d9f706163ac23b6dc93c449e0b7ee62528fa80d9b00d21b51b21be58b863e"
        "7d25b0cf547d588ce18351483c237ffec7ed1795b4d98e7aaa65855e2c2731ac"
        "8d2833a95460aab9d9e59c5e16e35495073391045ff837b599b8a10797f62539"
        "98cf4417d6a3bc591da5173a7e2e4e5a41b2532352b3b949c02dd1895b7d2193"
        "3be3c40b010e64b78af732c851af0ffc08f28ec887c841d7551a074ea110d8c8"
        "0a5bde65e11159eda342f3257570e01f484d6870bd888378e42e57a1fde52f9d"
        "dd2ab5c59c2cd59c");
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, shake128_incremental_squeeze_matches_one_shot) {
    // Squeezing two halves back-to-back from one instance must
    // match a single 128-byte squeeze from another instance of
    // the same input. This pins down the absorb_pos bookkeeping
    // across the rate boundary.
    std::string m(200, 'q');

    neuro::pkg::sha3::Shake128 h1;
    h1.absorb(bytes_of(m));
    auto p1 = h1.squeeze_bytes(64);
    auto p2 = h1.squeeze_bytes(64);

    auto full = Shake128::oneshot(std::string_view{m}, 128);

    EXPECT_EQ(std::vector<std::uint8_t>(full.begin(), full.begin() + 64), p1);
    EXPECT_EQ(std::vector<std::uint8_t>(full.begin() + 64, full.end()),   p2);
}

// ---- SHAKE256 -----------------------------------------------------------

TEST(sha3_family, shake256_empty_64_bytes) {
    auto got = Shake256::oneshot(std::span<const std::byte>{}, 64);
    auto expected = bytes_from_hex(
        "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"
        "d75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be");
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, shake256_abc_64_bytes) {
    auto got = Shake256::oneshot(std::string_view{"abc"}, 64);
    auto expected = bytes_from_hex(
        "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"
        "d5a15bef186a5386c75744c0527e1faa9f8726e462a12a4feb06bd8801e751e4");
    EXPECT_EQ(expected, got);
}

TEST(sha3_family, shake256_extendable_output_crosses_rate) {
    // SHAKE256 rate = 136 bytes. Squeeze 200 bytes to force a
    // second permutation.
    auto got = Shake256::oneshot(std::string_view{std::string(200, 'q')}, 200);
    auto expected = bytes_from_hex(
        "bb1f038db355538e4ec1593de48ce4bed5f498cfa125f38bf6a1c40a136eff53"
        "d2dfa07eed525f7f6af863e41e7d588bb2416656dfe1cf500597a416b4fd0298"
        "2e831c15468e33bfeee02688d172d51b1c5d20522264c4a1b23e558d98e492a3"
        "2ce79194e75c9ee7292bb43ed64bfa468911e54d95282be6ef328177dea79875"
        "e436717ec242908f2d9dad06f5433e4ea183818fccae5b71d293be61934ea667"
        "a2b7f3b282a9ba774d7256a4748ac3c717e02b59e16b1cd8abdbdbbeb7698fbe"
        "0d6789c505afd1ba");
    EXPECT_EQ(expected, got);
}

// ---- Digest types are size-distinct (compile-time check) ----------------

TEST(sha3_family, digest_types_have_distinct_sizes) {
    EXPECT_EQ(static_cast<std::size_t>(28), Digest224{}.size());
    EXPECT_EQ(static_cast<std::size_t>(32), Digest256{}.size());
    EXPECT_EQ(static_cast<std::size_t>(48), Digest384{}.size());
    EXPECT_EQ(static_cast<std::size_t>(64), Digest512{}.size());
}

RUN_ALL_TESTS()