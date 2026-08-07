// neuro/pkg/sha3.hpp
//
// SHA3-512 (FIPS 202) — header-only host implementation.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <vector>

#include "neuro/pkg/store.hpp"   // Digest

namespace neuro::pkg::sha3 {

// ---- Keccak-f[1600] permutation -----------------------------------------
//
// State: 5 × 5 × 64 bits. Lane (x, y) is at index x + 5*y.

namespace detail {

using Lane = std::uint64_t;
using State = std::array<Lane, 25>;

inline Lane rotl(Lane x, unsigned n) noexcept {
    return n ? ((x << n) | (x >> (64 - n))) : x;
}

inline State keccak_f1600(State s) noexcept {
    static constexpr Lane RC[24] = {
        0x0000000000000001ULL, 0x0000000000008082ULL,
        0x800000000000808AULL, 0x8000000080008000ULL,
        0x000000000000808BULL, 0x0000000080000001ULL,
        0x8000000080008081ULL, 0x8000000000008009ULL,
        0x000000000000008AULL, 0x0000000000000088ULL,
        0x0000000080008009ULL, 0x000000008000000AULL,
        0x000000008000808BULL, 0x800000000000008BULL,
        0x8000000000008089ULL, 0x8000000000008003ULL,
        0x8000000000008002ULL, 0x8000000000000080ULL,
        0x000000000000800AULL, 0x800000008000000AULL,
        0x8000000080008081ULL, 0x8000000000008080ULL,
        0x0000000080000001ULL, 0x8000000080008008ULL,
    };

    for (unsigned round = 0; round < 24; ++round) {
        // θ (theta)
        Lane c0 = s[0] ^ s[5] ^ s[10] ^ s[15] ^ s[20];
        Lane c1 = s[1] ^ s[6] ^ s[11] ^ s[16] ^ s[21];
        Lane c2 = s[2] ^ s[7] ^ s[12] ^ s[17] ^ s[22];
        Lane c3 = s[3] ^ s[8] ^ s[13] ^ s[18] ^ s[23];
        Lane c4 = s[4] ^ s[9] ^ s[14] ^ s[19] ^ s[24];
        Lane d0 = c4 ^ rotl(c1, 1);
        Lane d1 = c0 ^ rotl(c2, 1);
        Lane d2 = c1 ^ rotl(c3, 1);
        Lane d3 = c2 ^ rotl(c4, 1);
        Lane d4 = c3 ^ rotl(c0, 1);
        for (unsigned i = 0; i < 25; i += 5) {
            s[i + 0] ^= d0;
            s[i + 1] ^= d1;
            s[i + 2] ^= d2;
            s[i + 3] ^= d3;
            s[i + 4] ^= d4;
        }

        // ρ (rho) + π (pi). The walker (x, y) → (y, (2x+3y) mod 5)
        // starting from (1, 0) visits every lane except (0, 0).
        // We use the explicit two-pass form to make the
        // read-after-write hazard impossible: read all sources from
        // `s`, write all targets into a fresh `t`. Lane (0, 0) is
        // fixed by identity (offset 0).
        static constexpr unsigned kOffset[25] = {
             0,  1, 62, 28, 27,
            36, 44,  6, 55, 20,
             3, 10, 43, 25, 39,
            41, 45, 15, 21,  8,
            18,  2, 61, 56, 14,
        };
        // (src, tgt) pairs in walker order. lane index = x + 5*y.
        static constexpr unsigned kSrc[24] = {
            1,  5, 14, 18, 22,
            2,  6, 10, 19, 23,
            3,  7, 11, 15, 24,
            4,  8, 12, 16, 20,
           21, 17, 13,  9,
        };
        static constexpr unsigned kTgt[24] = {
           10, 16, 22,  3,  9,
           20,  1,  7, 13, 19,
            5, 11, 17, 23,  4,
           15, 21,  2,  8, 14,
           24, 18, 12,  6,
        };
        State t = s;
        for (unsigned k = 0; k < 24; ++k) {
            t[kTgt[k]] = rotl(s[kSrc[k]], kOffset[kSrc[k]]);
        }
        s = t;

        // χ (chi). Operate on rows (y = 0..4). For each row, capture
        // the 5 lane values into temporaries, then apply chi.
        for (unsigned y = 0; y < 25; y += 5) {
            Lane c0 = s[y + 0], c1 = s[y + 1], c2 = s[y + 2],
                 c3 = s[y + 3], c4 = s[y + 4];
            s[y + 0] = c0 ^ ((~c1) & c2);
            s[y + 1] = c1 ^ ((~c2) & c3);
            s[y + 2] = c2 ^ ((~c3) & c4);
            s[y + 3] = c3 ^ ((~c4) & c0);
            s[y + 4] = c4 ^ ((~c0) & c1);
        }

        // ι (iota)
        s[0] ^= RC[round];
    }
    return s;
}

inline void absorb_block(State& state, const std::byte* data) noexcept {
    for (std::size_t i = 0; i < 72; ++i) {
        std::size_t lane = i >> 3;
        std::size_t byte = i & 7;
        Lane mask = static_cast<Lane>(
                        static_cast<std::uint8_t>(data[i]))
                    << (8 * byte);
        state[lane] ^= mask;
    }
    state = keccak_f1600(state);
}

inline void squeeze_bytes(const State& state, std::byte* out,
                          std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t lane = i >> 3;
        std::size_t byte = i & 7;
        out[i] = static_cast<std::byte>(
            (state[lane] >> (8 * byte)) & 0xFF);
    }
}

}  // namespace detail

// ---- SHA3-512 hasher ----------------------------------------------------

class Sha3_512 {
public:
    static constexpr std::size_t kRateBytes   = 72;
    static constexpr std::size_t kOutputBytes = 64;

    Sha3_512() noexcept = default;

    void reset() noexcept {
        state_ = {};
        buffered_ = 0;
        finalized_ = false;
    }

    void absorb(std::span<const std::byte> data) noexcept {
        if (finalized_) return;
        const std::byte* p = data.data();
        std::size_t n = data.size();

        if (buffered_ > 0) {
            std::size_t take = std::min(kRateBytes - buffered_, n);
            std::memcpy(buf_.data() + buffered_, p, take);
            buffered_ += take;
            p += take;
            n -= take;
            if (buffered_ == kRateBytes) {
                detail::absorb_block(state_, buf_.data());
                buffered_ = 0;
            }
        }

        while (n >= kRateBytes) {
            detail::absorb_block(state_, p);
            p += kRateBytes;
            n -= kRateBytes;
        }

        if (n > 0) {
            std::memcpy(buf_.data() + buffered_, p, n);
            buffered_ += n;
        }
    }

    [[nodiscard]] Digest squeeze() noexcept {
        if (!finalized_) {
            std::memset(buf_.data() + buffered_, 0,
                        kRateBytes - buffered_);
            buf_[buffered_]      = std::byte{0x06};
            buf_[kRateBytes - 1] = std::byte{0x80};
            detail::absorb_block(state_, buf_.data());
            buffered_ = 0;
            finalized_ = true;
        }
        Digest out{};
        detail::squeeze_bytes(state_, reinterpret_cast<std::byte*>(out.data()),
                              kOutputBytes);
        return out;
    }

    static Digest oneshot(std::span<const std::byte> data) noexcept {
        Sha3_512 h;
        h.absorb(data);
        return h.squeeze();
    }

    static Digest oneshot(std::string_view s) noexcept {
        return oneshot(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(s.data()), s.size()));
    }

private:
    detail::State                  state_{};
    std::array<std::byte, kRateBytes> buf_{};
    std::size_t                    buffered_  = 0;
    bool                           finalized_ = false;
};

inline Digest sha3_512(std::span<const std::byte> data) noexcept {
    return Sha3_512::oneshot(data);
}

inline Digest sha3_512(std::string_view s) noexcept {
    return Sha3_512::oneshot(s);
}

inline Digest sha3_512_file(const std::filesystem::path& p) noexcept {
    std::ifstream f(p, std::ios::binary);
    if (!f) return Digest{};
    Sha3_512 h;
    std::array<std::byte, 4096> chunk;
    while (f) {
        f.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        auto got = static_cast<std::size_t>(f.gcount());
        if (got == 0) break;
        h.absorb(std::span<const std::byte>(chunk.data(), got));
    }
    return h.squeeze();
}

inline bool verify(const Digest& d, std::span<const std::byte> bytes) noexcept {
    return sha3_512(bytes) == d;
}

}  // namespace neuro::pkg::sha3