// neuro/pkg/sha3.hpp
//
// SHA3 family (FIPS 202) — header-only host implementation.
//
// Provides:
//   * FIPS 202 fixed-output hash: SHA3-224, SHA3-256, SHA3-384,
//     SHA3-512. Padding byte is 0x06 (pad10*1).
//   * FIPS 202 extendable-output: SHAKE128, SHAKE256. Padding byte
//     is 0x1F and the caller chooses how many bytes to squeeze.
//
// All four flavors share the same Keccak-f[1600] permutation; only
// the sponge (rate, capacity, output length) differs. The state is
// 5×5×64 bits laid out as 25 little-endian-packed lanes.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "neuro/pkg/digest.hpp"

namespace neuro::pkg::sha3 {

// ---- Keccak-f[1600] permutation -----------------------------------------

namespace detail {

using Lane  = std::uint64_t;
using State = std::array<Lane, 25>;

inline Lane rotl(Lane x, unsigned n) noexcept {
    return n ? ((x << n) | (x >> (64 - n))) : x;
}

inline State keccak_f1600(State s) noexcept {
    // Round constants per FIPS 202 §3.2.5.
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

    // Lane offset table per FIPS 202 §3.2.2.
    static constexpr unsigned kOffset[25] = {
         0,  1, 62, 28, 27,
        36, 44,  6, 55, 20,
         3, 10, 43, 25, 39,
        41, 45, 15, 21,  8,
        18,  2, 61, 56, 14,
    };
    // (src, tgt) pairs in lane-walking order; (0,0) is fixed.
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

        // ρ (rho) + π (pi) — explicit two-pass form to avoid the
        // read-after-write hazard.
        State t = s;
        for (unsigned k = 0; k < 24; ++k) {
            t[kTgt[k]] = rotl(s[kSrc[k]], kOffset[kSrc[k]]);
        }
        s = t;

        // χ (chi).
        for (unsigned y = 0; y < 25; y += 5) {
            Lane a = s[y + 0], b = s[y + 1], c = s[y + 2],
                 d = s[y + 3], e = s[y + 4];
            s[y + 0] = a ^ ((~b) & c);
            s[y + 1] = b ^ ((~c) & d);
            s[y + 2] = c ^ ((~d) & e);
            s[y + 3] = d ^ ((~e) & a);
            s[y + 4] = e ^ ((~a) & b);
        }

        // ι (iota).
        s[0] ^= RC[round];
    }
    return s;
}

// Absorb one rate-block of bytes into the state.
inline void absorb_block(State& state,
                         const std::byte* data,
                         std::size_t rate_bytes) noexcept {
    for (std::size_t i = 0; i < rate_bytes; ++i) {
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

// SHA3 family members are exposed through one template
// parameterised by the two sponge parameters (rate, capacity) plus
// the FIPS 202 padding byte.
//
//   SHA3-224: rate = 1152 bits = 144 bytes, output = 28 bytes
//   SHA3-256: rate = 1088 bits = 136 bytes, output = 32 bytes
//   SHA3-384: rate =  832 bits = 104 bytes, output = 48 bytes
//   SHA3-512: rate =  576 bits =  72 bytes, output = 64 bytes
//   SHAKE128: rate = 1344 bits = 168 bytes, output = variable
//   SHAKE256: rate = 1088 bits = 136 bytes, output = variable
//
// Both fixed-output and extendable-output use the same template
// machinery; the only difference is that SHAKE allows multi-squeeze
// after the pad.

template <std::size_t kRateBytes, std::size_t kOutputBytes,
          std::uint8_t kPadByte>
class Sha3Base {
public:
    static constexpr std::size_t rate_bytes()   noexcept { return kRateBytes; }
    static constexpr std::size_t output_bytes() noexcept { return kOutputBytes; }

    Sha3Base() noexcept = default;

    void reset() noexcept {
        state_ = {};
        buffered_  = 0;
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
                absorb_into_state_();
                buffered_ = 0;
            }
        }

        while (n >= kRateBytes) {
            detail::absorb_block(state_, p, kRateBytes);
            p += kRateBytes;
            n -= kRateBytes;
        }

        if (n > 0) {
            std::memcpy(buf_.data() + buffered_, p, n);
            buffered_ += n;
        }
    }

private:
    void absorb_into_state_() noexcept {
        detail::absorb_block(state_, buf_.data(), kRateBytes);
    }

protected:
    State                         state_{};
    std::array<std::byte, kRateBytes> buf_{};
    std::size_t                   buffered_  = 0;
    std::size_t                   absorb_pos_ = 0;  // for multi-squeeze
    bool                          finalized_ = false;
};

}  // namespace detail

// ---- Public API: FIPS-202 fixed-output SHA3 -----------------------------
//
// `Sha3T<OutputBytes, RateBytes>` is parameterised by the sponge
// dimensions; it returns a DigestN<OutputBytes>. The four FIPS-202
// fixed-output members are typedef'd for clarity.

template <std::size_t kOutputBytes, std::size_t kRateBytes>
class Sha3T
    : public detail::Sha3Base<kRateBytes, kOutputBytes, /*kPad*/ 0x06> {
public:
    using DigestT = DigestN<kOutputBytes>;

    [[nodiscard]] static DigestT
    oneshot(std::span<const std::byte> data) noexcept {
        Sha3T h;
        h.absorb(data);
        return h.final();
    }

    [[nodiscard]] static DigestT
    oneshot(std::string_view s) noexcept {
        return oneshot(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(s.data()), s.size()));
    }

    // Finalize and produce the digest. Idempotent: subsequent calls
    // return the same digest without re-finalizing.
    [[nodiscard]] DigestT final() noexcept {
        if (!this->finalized_) {
            std::memset(this->buf_.data() + this->buffered_, 0,
                        kRateBytes - this->buffered_);
            this->buf_[this->buffered_]      = std::byte{0x06};
            this->buf_[kRateBytes - 1]       = std::byte{0x80};
            detail::absorb_block(this->state_,
                                 this->buf_.data(), kRateBytes);
            this->buffered_  = 0;
            this->absorb_pos_ = 0;
            this->finalized_ = true;
        }
        DigestT out{};
        std::array<std::byte, kRateBytes> scratch{};
        detail::squeeze_bytes(this->state_, scratch.data(), kRateBytes);
        std::memcpy(out.data(), scratch.data(), kOutputBytes);
        return out;
    }
};

using Sha3_224 = Sha3T<28, 144>;
using Sha3_256 = Sha3T<32, 136>;
using Sha3_384 = Sha3T<48, 104>;
using Sha3_512 = Sha3T<64,  72>;

// ---- Public API: FIPS-202 SHAKE (extendable output) ---------------------
//
// SHAKE is the same sponge with padding byte 0x1F and unbounded
// output. We expose it via `class Shake<R>`. The caller chooses the
// output length at squeeze time.

template <std::size_t kRateBytes>
class Shake : public detail::Sha3Base<kRateBytes, /*kOutput*/ 0, /*kPad*/ 0x1F> {
public:
    static constexpr std::size_t rate_bytes() noexcept { return kRateBytes; }

    [[nodiscard]] static std::vector<std::uint8_t>
    oneshot(std::span<const std::byte> data, std::size_t n_bytes) noexcept {
        Shake h;
        h.absorb(data);
        return h.squeeze_bytes(n_bytes);
    }

    [[nodiscard]] static std::vector<std::uint8_t>
    oneshot(std::string_view s, std::size_t n_bytes) noexcept {
        return oneshot(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(s.data()), s.size()), n_bytes);
    }

    [[nodiscard]] std::vector<std::uint8_t>
    squeeze_bytes(std::size_t n_bytes) noexcept {
        if (!this->finalized_) {
            std::memset(this->buf_.data() + this->buffered_, 0,
                        kRateBytes - this->buffered_);
            this->buf_[this->buffered_]      = std::byte{0x1F};
            this->buf_[kRateBytes - 1]       = std::byte{0x80};
            detail::absorb_block(this->state_,
                                 this->buf_.data(), kRateBytes);
            this->buffered_  = 0;
            this->absorb_pos_ = 0;
            this->finalized_ = true;
        }
        std::vector<std::uint8_t> out(n_bytes);
        std::array<std::byte, kRateBytes> scratch{};
        std::size_t produced = 0;
        while (produced < n_bytes) {
            detail::squeeze_bytes(this->state_,
                                  scratch.data(), kRateBytes);
            std::size_t take = std::min(kRateBytes, n_bytes - produced);
            std::memcpy(out.data() + produced,
                        scratch.data() + this->absorb_pos_, take);
            produced += take;
            this->absorb_pos_ += take;
            if (this->absorb_pos_ == kRateBytes) {
                this->absorb_pos_ = 0;
                this->state_ = detail::keccak_f1600(this->state_);
            }
        }
        return out;
    }
};

using Shake128 = Shake<168>;  // capacity = 256 bits
using Shake256 = Shake<136>;  // capacity = 512 bits

// ---- Convenience free functions -----------------------------------------

inline Digest224 sha3_224(std::span<const std::byte> data) noexcept {
    return Sha3_224::oneshot(data);
}
inline Digest224 sha3_224(std::string_view s) noexcept {
    return Sha3_224::oneshot(s);
}

inline Digest256 sha3_256(std::span<const std::byte> data) noexcept {
    return Sha3_256::oneshot(data);
}
inline Digest256 sha3_256(std::string_view s) noexcept {
    return Sha3_256::oneshot(s);
}

inline Digest384 sha3_384(std::span<const std::byte> data) noexcept {
    return Sha3_384::oneshot(data);
}
inline Digest384 sha3_384(std::string_view s) noexcept {
    return Sha3_384::oneshot(s);
}

inline Digest512 sha3_512(std::span<const std::byte> data) noexcept {
    return Sha3_512::oneshot(data);
}
inline Digest512 sha3_512(std::string_view s) noexcept {
    return Sha3_512::oneshot(s);
}

inline Digest512 sha3_512_file(const std::filesystem::path& p) noexcept {
    std::ifstream f(p, std::ios::binary);
    if (!f) return Digest512{};
    Sha3_512 h;
    std::array<std::byte, 4096> chunk;
    while (f) {
        f.read(reinterpret_cast<char*>(chunk.data()), chunk.size());
        auto got = static_cast<std::size_t>(f.gcount());
        if (got == 0) break;
        h.absorb(std::span<const std::byte>(chunk.data(), got));
    }
    return h.final();
}

inline bool verify(const Digest& d, std::span<const std::byte> bytes) noexcept {
    return sha3_512(bytes) == d;
}

}  // namespace neuro::pkg::sha3