// neuro/net/address.hpp
//
// Socket address types.
//
// Per README §4.6 (NeuroNet):
//   - IPv4 is the only host-scaffold address family; IPv6 lands
//     alongside the kernel net stack in Phase 1.
//   - SocketAddress is a discriminated union (kind + port), so
//     a single call site can accept "any address" without losing
//     the family information.
//   - IpAddr is a value type with equality + a std::string parser
//     so DNS stubs and tests can both consume it.
//
// We keep the on-the-wire layout identical to POSIX
// (struct sockaddr_in / sockaddr_in6) so the kernel net stack can
// reuse the same headers in Phase 1.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace neuro::net {

class IpAddr {
public:
    enum class Kind { V4 };

    constexpr IpAddr() noexcept : kind_(Kind::V4), v4_{0} {}

    // Construct an IPv4 address from four octets.
    constexpr IpAddr(std::uint8_t a, std::uint8_t b,
                     std::uint8_t c, std::uint8_t d) noexcept
        : kind_(Kind::V4), v4_{a, b, c, d} {}

    // Construct from a 32-bit host-order value.
    constexpr explicit IpAddr(std::uint32_t host_order) noexcept
        : kind_(Kind::V4) {
        v4_[0] = static_cast<std::uint8_t>((host_order >> 24) & 0xFF);
        v4_[1] = static_cast<std::uint8_t>((host_order >> 16) & 0xFF);
        v4_[2] = static_cast<std::uint8_t>((host_order >>  8) & 0xFF);
        v4_[3] = static_cast<std::uint8_t>( host_order        & 0xFF);
    }

    [[nodiscard]] constexpr Kind kind() const noexcept { return kind_; }
    [[nodiscard]] constexpr const std::uint8_t* bytes() const noexcept {
        return v4_.data();
    }
    [[nodiscard]] constexpr std::uint32_t host_order() const noexcept {
        return (std::uint32_t(v4_[0]) << 24) |
               (std::uint32_t(v4_[1]) << 16) |
               (std::uint32_t(v4_[2]) <<  8) |
                std::uint32_t(v4_[3]);
    }

    // Parse a dotted-quad string ("192.0.2.1"). Throws on malformed
    // input — invalid addresses are programming errors, not runtime
    // conditions on the host scaffold.
    static IpAddr parse(std::string_view s) {
        std::uint8_t octets[4] = {0, 0, 0, 0};
        std::size_t pos = 0;
        for (int i = 0; i < 4; ++i) {
            std::size_t end = s.find('.', pos);
            std::size_t part_len = (end == std::string_view::npos)
                                       ? s.size() - pos
                                       : end - pos;
            std::string_view part = s.substr(pos, part_len);
            if (part.empty()) {
                throw std::invalid_argument("IpAddr::parse: empty octet");
            }
            // Manual decimal parse — we cannot rely on std::from_chars
            // being <charconv>-supported on every toolchain, and a
            // small loop is easier to reason about.
            unsigned v = 0;
            for (char ch : part) {
                if (ch < '0' || ch > '9') {
                    throw std::invalid_argument(
                        "IpAddr::parse: non-digit octet");
                }
                v = v * 10 + unsigned(ch - '0');
                if (v > 255) {
                    throw std::invalid_argument(
                        "IpAddr::parse: octet > 255");
                }
            }
            octets[i] = static_cast<std::uint8_t>(v);
            if (i < 3) {
                if (end == std::string_view::npos) {
                    throw std::invalid_argument(
                        "IpAddr::parse: missing '.'");
                }
                pos = end + 1;
            } else {
                if (end != std::string_view::npos) {
                    throw std::invalid_argument(
                        "IpAddr::parse: trailing '.'");
                }
            }
        }
        return IpAddr{octets[0], octets[1], octets[2], octets[3]};
    }

    [[nodiscard]] std::string to_string() const {
        std::string out;
        out.reserve(15);
        out += std::to_string(v4_[0]);
        out += '.';
        out += std::to_string(v4_[1]);
        out += '.';
        out += std::to_string(v4_[2]);
        out += '.';
        out += std::to_string(v4_[3]);
        return out;
    }

    friend constexpr bool operator==(IpAddr a, IpAddr b) noexcept {
        return a.host_order() == b.host_order();
    }
    friend constexpr bool operator!=(IpAddr a, IpAddr b) noexcept {
        return !(a == b);
    }

private:
    Kind                  kind_;
    std::array<std::uint8_t, 4> v4_;
};

// A transport endpoint (IP + port). Host byte order on the port so
// callers do not need to think about htons/ntohs.
struct SocketAddress {
    IpAddr                  ip;
    std::uint16_t           port = 0;

    constexpr SocketAddress() noexcept = default;
    constexpr SocketAddress(IpAddr ip_, std::uint16_t port_) noexcept
        : ip(ip_), port(port_) {}

    // Loopback / any.
    static constexpr SocketAddress loopback(std::uint16_t port) noexcept {
        return SocketAddress{IpAddr{127, 0, 0, 1}, port};
    }
    static constexpr SocketAddress any_v4(std::uint16_t port) noexcept {
        return SocketAddress{IpAddr{0, 0, 0, 0}, port};
    }

    friend constexpr bool operator==(SocketAddress a, SocketAddress b) noexcept {
        return a.ip == b.ip && a.port == b.port;
    }
    friend constexpr bool operator!=(SocketAddress a, SocketAddress b) noexcept {
        return !(a == b);
    }

    [[nodiscard]] std::string to_string() const {
        return ip.to_string() + ":" + std::to_string(port);
    }
};

}  // namespace neuro::net
