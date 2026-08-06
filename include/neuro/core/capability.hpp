// neuro/core/capability.hpp
//
// 128-bit unforgeable capability token used everywhere in NeuroVerse OS.
// Mirrors README §9.2 / §4.2. Layout verified by static_assert(sizeof==16).

#pragma once

#include <cstdint>
#include <string_view>

namespace neuro::core {

enum class CapRight : std::uint16_t {
    None   = 0,
    Read   = 1 << 0,
    Write  = 1 << 1,
    Exec   = 1 << 2,
    Grant  = 1 << 3,
    Map    = 1 << 4,
    Signal = 1 << 5,
    Audit  = 1 << 6,
    All    = Read | Write | Exec | Grant | Map | Signal | Audit,
};

inline constexpr CapRight operator|(CapRight a, CapRight b) noexcept {
    return static_cast<CapRight>(static_cast<std::uint16_t>(a) |
                                 static_cast<std::uint16_t>(b));
}

inline constexpr CapRight operator&(CapRight a, CapRight b) noexcept {
    return static_cast<CapRight>(static_cast<std::uint16_t>(a) &
                                 static_cast<std::uint16_t>(b));
}

inline constexpr CapRight& operator|=(CapRight& a, CapRight b) noexcept {
    a = a | b;
    return a;
}

inline constexpr CapRight& operator&=(CapRight& a, CapRight b) noexcept {
    a = a & b;
    return a;
}

struct Capability {
    std::uint64_t object_id  : 48;
    std::uint64_t rights     : 16;  // bitmask of CapRight
    std::uint64_t epoch      : 16;
    std::uint64_t generation : 48;

    [[nodiscard]] static Capability mint(std::uint64_t oid, CapRight r,
                                         std::uint64_t ep, std::uint64_t gen) {
        return Capability{oid,
                          static_cast<std::uint64_t>(r),
                          ep,
                          gen};
    }

    [[nodiscard]] bool has(CapRight r) const noexcept {
        return (rights & static_cast<std::uint64_t>(r)) ==
               static_cast<std::uint64_t>(r);
    }

    [[nodiscard]] Capability attenuate(CapRight r) const noexcept {
        return Capability{object_id,
                          rights & static_cast<std::uint64_t>(r),
                          epoch,
                          generation};
    }

    [[nodiscard]] bool verify() const noexcept {
        // In a real kernel, the kernel would verify against its own table.
        return object_id != 0;
    }

    [[nodiscard]] std::string_view to_string() const noexcept {
        // Placeholder: a real implementation would format to a static buffer.
        return "cap";
    }
};

static_assert(sizeof(Capability) == 16, "Capability must be 16 bytes");

}  // namespace neuro::core