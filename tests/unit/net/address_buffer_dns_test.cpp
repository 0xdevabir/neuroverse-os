// tests/unit/net/address_buffer_dns_test.cpp
//
// Tests for neuro::net::IpAddr, SocketAddress, NetBuffer, and the
// DNS helper.
//
// Coverage:
//   IpAddr:
//     - default ctor is 0.0.0.0
//     - 4-octet ctor
//     - host_order() round-trip
//     - bytes() returns the underlying array
//     - parse() succeeds for valid dotted-quad strings
//     - parse() throws on malformed input (empty, non-digit, > 255, etc.)
//     - to_string() round-trips with parse()
//     - equality / inequality
//   SocketAddress:
//     - default ctor
//     - loopback / any_v4 factories
//     - to_string() emits ip:port
//     - equality
//   NetBuffer:
//     - default-constructed is empty
//     - size ctor fills with zeros
//     - (ptr, n) ctor copies bytes
//     - from_string builds correct buffer
//     - as_string recovers the text
//     - append concatenates
//     - resize / reserve / clear
//     - equality
//   Dns:
//     - resolve_one_v4("localhost") returns 127.0.0.1

#include "neuro/net/address.hpp"
#include "neuro/net/buffer.hpp"
#include "neuro/net/dns.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include "../../test_framework.hpp"

// EXPECT_THROW: no native equivalent in test_framework.hpp
#define EXPECT_THROW(expr, ExType)                                        \
    do {                                                                  \
        bool _threw = false;                                              \
        try { (void)(expr); }                                             \
        catch (const ExType&) { _threw = true; }                          \
        catch (...) {}                                                    \
        if (!_threw) {                                                    \
            throw std::runtime_error(std::string(__FILE__) + ":" +        \
                                     std::to_string(__LINE__) +           \
                                     " EXPECT_THROW failed: " #expr);     \
        }                                                                 \
    } while (0)

using neuro::net::Dns;
using neuro::net::IpAddr;
using neuro::net::NetBuffer;
using neuro::net::SocketAddress;

// ---- IpAddr --------------------------------------------------------

TEST(net, ipaddr_default_is_zero) {
    IpAddr a{};
    EXPECT_EQ(0u, a.host_order());
    EXPECT_EQ(IpAddr::Kind::V4, a.kind());
}

TEST(net, ipaddr_octet_ctor) {
    IpAddr a(192, 0, 2, 1);
    EXPECT_EQ(static_cast<std::uint32_t>(0xC0000201u), a.host_order());
    EXPECT_EQ(static_cast<std::uint8_t>(192), a.bytes()[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(1),   a.bytes()[3]);
}

TEST(net, ipaddr_host_order_round_trip) {
    IpAddr a(0xDEADBEEFu);
    EXPECT_EQ(static_cast<std::uint32_t>(0xDEADBEEFu), a.host_order());
    EXPECT_EQ(static_cast<std::uint8_t>(0xDE), a.bytes()[0]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xAD), a.bytes()[1]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xBE), a.bytes()[2]);
    EXPECT_EQ(static_cast<std::uint8_t>(0xEF), a.bytes()[3]);
}

TEST(net, ipaddr_parse_valid) {
    auto a = IpAddr::parse("192.0.2.1");
    EXPECT_EQ(static_cast<std::uint32_t>(0xC0000201u), a.host_order());

    auto b = IpAddr::parse("127.0.0.1");
    EXPECT_EQ(static_cast<std::uint32_t>(0x7F000001u), b.host_order());

    auto c = IpAddr::parse("0.0.0.0");
    EXPECT_EQ(0u, c.host_order());

    auto d = IpAddr::parse("255.255.255.255");
    EXPECT_EQ(static_cast<std::uint32_t>(0xFFFFFFFFu), d.host_order());
}

TEST(net, ipaddr_parse_throws_on_bad_input) {
    EXPECT_THROW(IpAddr::parse(""),                std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("1.2.3"),           std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("1.2.3.4.5"),       std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("1.2.3."),          std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("1..2.3"),          std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("a.b.c.d"),          std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("1.2.3.256"),        std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("999.0.0.0"),       std::invalid_argument);
    EXPECT_THROW(IpAddr::parse("-1.0.0.0"),        std::invalid_argument);
}

TEST(net, ipaddr_to_string_round_trip) {
    auto a = IpAddr::parse("203.0.113.42");
    EXPECT_EQ(std::string("203.0.113.42"), a.to_string());
    auto b = IpAddr::parse(a.to_string());
    EXPECT_EQ(a, b);
}

TEST(net, ipaddr_equality) {
    IpAddr a(10, 0, 0, 1);
    IpAddr b(10, 0, 0, 1);
    IpAddr c(10, 0, 0, 2);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_TRUE(a != c);
    EXPECT_FALSE(a == c);
}

// ---- SocketAddress -------------------------------------------------

TEST(net, sock_default_ctor) {
    SocketAddress s{};
    EXPECT_EQ(0u, s.ip.host_order());
    EXPECT_EQ(0u, s.port);
}

TEST(net, sock_loopback_factory) {
    auto s = SocketAddress::loopback(8080);
    EXPECT_EQ(8080u, s.port);
    EXPECT_EQ(static_cast<std::uint32_t>(0x7F000001u), s.ip.host_order());
}

TEST(net, sock_any_v4_factory) {
    auto s = SocketAddress::any_v4(443);
    EXPECT_EQ(443u, s.port);
    EXPECT_EQ(0u, s.ip.host_order());
}

TEST(net, sock_to_string) {
    auto s = SocketAddress(IpAddr(192, 168, 1, 1), 22u);
    EXPECT_EQ(std::string("192.168.1.1:22"), s.to_string());
}

TEST(net, sock_equality) {
    auto a = SocketAddress(IpAddr(1, 2, 3, 4), 5);
    auto b = SocketAddress(IpAddr(1, 2, 3, 4), 5);
    auto c = SocketAddress(IpAddr(1, 2, 3, 4), 6);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

// ---- NetBuffer -----------------------------------------------------

TEST(net, buf_default_empty) {
    NetBuffer b;
    EXPECT_TRUE(b.empty());
    EXPECT_EQ(0u, b.size());
}

TEST(net, buf_size_ctor) {
    NetBuffer b(16);
    EXPECT_EQ(16u, b.size());
    EXPECT_FALSE(b.empty());
}

TEST(net, buf_ptr_size_ctor) {
    const std::uint8_t src[] = {0xDE, 0xAD, 0xBE, 0xEF};
    NetBuffer b(reinterpret_cast<const std::byte*>(src), 4);
    EXPECT_EQ(4u, b.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0xDE),
              static_cast<std::uint8_t>(b.data()[0]));
}

TEST(net, buf_from_string_literal) {
    auto b = NetBuffer::from_string("hello");
    EXPECT_EQ(5u, b.size());
    EXPECT_EQ(std::string("hello"), b.as_string());
}

TEST(net, buf_from_std_string) {
    std::string s = "neuroverse";
    auto b = NetBuffer::from_string(s);
    EXPECT_EQ(10u, b.size());
    EXPECT_EQ(std::string("neuroverse"), b.as_string());
}

TEST(net, buf_append_concatenates) {
    NetBuffer a = NetBuffer::from_string("foo");
    NetBuffer b = NetBuffer::from_string("bar");
    a.append(b);
    EXPECT_EQ(6u, a.size());
    EXPECT_EQ(std::string("foobar"), a.as_string());

    const std::uint8_t tail[] = {0x21, 0x22};
    a.append(reinterpret_cast<const std::byte*>(tail), 2);
    EXPECT_EQ(8u, a.size());
    EXPECT_EQ(static_cast<std::uint8_t>(0x21),
              static_cast<std::uint8_t>(a.data()[6]));
}

TEST(net, buf_resize_reserve_clear) {
    NetBuffer b;
    b.resize(10);
    EXPECT_EQ(10u, b.size());
    b.clear();
    EXPECT_TRUE(b.empty());
    b.reserve(100);
    EXPECT_EQ(0u, b.size());
}

TEST(net, buf_equality) {
    auto a = NetBuffer::from_string("same");
    auto b = NetBuffer::from_string("same");
    auto c = NetBuffer::from_string("diff");
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

// ---- Dns ------------------------------------------------------------

TEST(net, dns_resolve_localhost_is_loopback) {
    auto ip = Dns::resolve_one_v4("localhost");
    EXPECT_EQ(static_cast<std::uint32_t>(0x7F000001u), ip.host_order());
}

TEST(net, dns_resolve_v4_returns_at_least_one) {
    auto v = Dns::resolve_v4("localhost");
    EXPECT_TRUE(!v.empty());
}

TEST(net, dns_resolve_unknown_throws) {
    EXPECT_THROW(
        Dns::resolve_one_v4("this-host-definitely-does-not-exist.invalid"),
        std::runtime_error);
}

// Z6.6: resolve_v4 throws on the same bad hostname.
TEST(net, dns_resolve_v4_unknown_throws) {
    EXPECT_THROW(
        Dns::resolve_v4("this-host-definitely-does-not-exist.invalid"),
        std::runtime_error);
}

TEST(net, dns_resolve_v4_returns_empty_vector_or_throws_on_unknown) {
    // Some implementations return an empty vector rather than throw;
    // either is acceptable for the test to pass. We check that the
    // call returns either an empty result or throws — never returns
    // a non-empty bogus result.
    bool threw = false;
    std::vector<IpAddr> v;
    try {
        v = Dns::resolve_v4("this-host-definitely-does-not-exist.invalid");
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw || v.empty());
}

RUN_ALL_TESTS()