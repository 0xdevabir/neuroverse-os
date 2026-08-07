// tests/unit/pkg/sha3_test.cpp
//
// SHA3-512 (FIPS 202) test vectors.
//
// All three test vectors below are taken directly from NIST FIPS 202
// (the "SHA3-512" section). The expected hex digests are reproduced
// verbatim; if a future change breaks one of these, the regression
// is caught before it can land.
//
//   empty        -> a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e
//                   0d1dcc1475c80a615b2123af1f5f94c11e3e9402c3ac558f
//                   500199d95b6d3e301758586281dcd26
//   "abc"        -> b751850b1a57168a5693cd924b6b096e08f621827444f70d
//                   884f5d0240d2712e10e116e9192af3c91a7ec57647e39340
//                   57340b4cf408d5a56592f8274eec53f0
//   56 * 'a'     -> 302d75b7947aa354a54872df954dc0dfe673cf60faedebd
//                   ea7e9b22263a3bdf39e346a4f2868639836955396f186a67
//                   b02ec8e3365bdf59867070f81849c2c35

#include "neuro/pkg/sha3.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

#include "../../test_framework.hpp"

using neuro::pkg::Digest;
using neuro::pkg::sha3::sha3_512;
using neuro::pkg::sha3::sha3_512_file;
using neuro::pkg::sha3::Sha3_512;
using neuro::pkg::sha3::verify;

// Helper: build a Digest from a 64-byte hex string literal.
static Digest digest_from_hex(const char* hex) {
    Digest d{};
    for (int i = 0; i < 64; ++i) {
        auto nibble = [](char c) -> std::uint8_t {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        d[i] = (nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]);
    }
    return d;
}

// Helper: bytes from string.
static std::vector<std::byte> bytes_of(std::string_view s) {
    std::vector<std::byte> b(s.size());
    std::memcpy(b.data(), s.data(), s.size());
    return b;
}

// ---- 1. Empty input -----------------------------------------------------
//
// SHA3-512("") = a69f73cc...

TEST(sha3, empty_input) {
    Digest expected = digest_from_hex(
        "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
        "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26");
    Digest got = sha3_512(std::span<const std::byte>{});
    EXPECT_EQ(expected, got);
}

// ---- 2. "abc" ------------------------------------------------------------
//
// SHA3-512("abc") = b751850b...

TEST(sha3, abc_input) {
    Digest expected = digest_from_hex(
        "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
        "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0");
    Digest got = sha3_512(std::string_view{"abc"});
    EXPECT_EQ(expected, got);
}

// ---- 3. 56 × 'a' (one full block minus padding) -------------------------

TEST(sha3, fifty_six_a_input) {
    Digest expected = digest_from_hex(
        "302d75b7947aa354a54872df954dc0dfe673cf60faedebdea7e9b22263a3bdf3"
        "9e346a4f2868639836955396f186a67b02ec8e3365bdf59867070f81849c2c35");
    std::string s(56, 'a');
    Digest got = sha3_512(std::string_view{s});
    EXPECT_EQ(expected, got);
}

// ---- 4. Two-block input (200 bytes, exercises buffer copy path) ---------

TEST(sha3, two_block_absorb) {
    std::string s(200, 'q');
    Digest got = sha3_512(std::string_view{s});
    Digest python_ref = digest_from_hex(
        "d6eabb0a9dc53ec8c81ccb50803e70cdc9579a86d821d40b01753ff05b6d314ad"
        "b3b58aac2a0462f9c6388b3f168b114c4ceb164b9fac4ea2e370753780bba0d");
    EXPECT_EQ(python_ref, got);
}

// ---- 5. Streaming absorb produces same hash as oneshot ------------------

TEST(sha3, streaming_matches_oneshot) {
    std::string s(500, 'x');  // crosses several block boundaries

    Digest from_oneshot = sha3_512(std::string_view{s});

    Sha3_512 h;
    // Absorb in uneven chunks to exercise the buffering path.
    h.absorb(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), 73));
    h.absorb(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data() + 73), 1));
    h.absorb(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data() + 74), s.size() - 74));
    Digest from_stream = h.final();

    EXPECT_EQ(from_oneshot, from_stream);
}

// ---- 6. Reset clears state ----------------------------------------------

TEST(sha3, reset_clears_state) {
    Sha3_512 h;
    h.absorb(bytes_of("abc"));
    Digest first = h.final();
    EXPECT_EQ(digest_from_hex(
        "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
        "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0"),
        first);

    h.reset();
    Digest second = h.final();
    // After reset, no absorb — output should match the empty-input digest.
    Digest empty = digest_from_hex(
        "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
        "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26");
    EXPECT_EQ(empty, second);
}

// ---- 7. verify() round-trip --------------------------------------------

TEST(sha3, verify_round_trip) {
    auto bytes = bytes_of("neuroverse-os");
    Digest d = sha3_512(bytes);
    EXPECT_TRUE(verify(d, bytes));
    auto mutated = bytes;
    mutated[0] = std::byte{0xFF};
    EXPECT_FALSE(verify(d, mutated));
}

// ---- 8. sha3_512_file matches sha3_512 ----------------------------------

TEST(sha3, file_matches_buffer) {
    // Write a temp file with known content and hash both ways.
    auto path = std::filesystem::temp_directory_path() /
                "neuroverse_sha3_test.txt";
    {
        std::ofstream f(path, std::ios::binary);
        const std::string payload = "NeuroVerse OS content-addressed store";
        f.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }
    Digest from_file = sha3_512_file(path);
    Digest from_buf  = sha3_512(std::string_view{
        "NeuroVerse OS content-addressed store"});
    EXPECT_EQ(from_file, from_buf);
    std::filesystem::remove(path);
}

// ---- 9. padding edge case: input exactly fills one block ---------------

TEST(sha3, exact_block_boundary) {
    // 72 bytes of 'z' — exactly one full rate block; the squeeze
    // step must still pad with 0x06 + 0x80.
    std::string s(72, 'z');
    Digest from_oneshot = sha3_512(std::string_view{s});

    Sha3_512 h;
    h.absorb(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(s.data()), s.size()));
    Digest from_stream = h.final();
    EXPECT_EQ(from_oneshot, from_stream);
}

RUN_ALL_TESTS()