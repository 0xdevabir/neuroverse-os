// neuro/pkg/digest.hpp
//
// Fixed-size hash digests used across NeuroPkg, NeuroSec, and
// NeuroBoot. The SHA3 family (FIPS 202) emits digests of various
// sizes — 224, 256, 384, and 512 bits — and SHAKE emits arbitrarily
// long output. Each digest size has its own type so the compiler
// can refuse to compare a SHA3-224 digest against a SHA3-256 digest.
//
// A single canonical alias `Digest` (= Digest512, the SHA3-512 size
// in bytes) remains for callers that don't care which size they get
// and want to interoperate with the older byte-array API.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace neuro::pkg {

template <std::size_t N>
struct DigestN {
    std::array<std::uint8_t, N> bytes{};

    constexpr std::size_t size() const noexcept { return N; }
    constexpr std::uint8_t*       data()       noexcept { return bytes.data(); }
    constexpr const std::uint8_t* data() const noexcept { return bytes.data(); }

    constexpr std::uint8_t&       operator[](std::size_t i)       noexcept { return bytes[i]; }
    constexpr const std::uint8_t& operator[](std::size_t i) const noexcept { return bytes[i]; }

    constexpr bool operator==(const DigestN&) const noexcept = default;
    constexpr bool operator!=(const DigestN&) const noexcept = default;

    // Lexicographic ordering (matches std::array's behavior so the
    // digest can be used as a std::map / std::set key).
    constexpr bool operator<(const DigestN& o) const noexcept {
        for (std::size_t i = 0; i < N; ++i) {
            if (bytes[i] != o.bytes[i]) return bytes[i] < o.bytes[i];
        }
        return false;
    }
};

// Canonical fixed-size digest aliases.
using Digest224 = DigestN<28>;   // SHA3-224
using Digest256 = DigestN<32>;   // SHA3-256
using Digest384 = DigestN<48>;   // SHA3-384
using Digest512 = DigestN<64>;   // SHA3-512

// Back-compat: `Digest` = the largest fixed digest (SHA3-512).
using Digest = Digest512;

template <std::size_t N>
inline std::string to_hex(const DigestN<N>& d) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out(d.size() * 2, '0');
    for (std::size_t i = 0; i < d.size(); ++i) {
        out[i * 2 + 0] = kHex[(d[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[d[i] & 0xF];
    }
    return out;
}

}  // namespace neuro::pkg