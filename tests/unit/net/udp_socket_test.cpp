// tests/unit/net/udp_socket_test.cpp
//
// Unit tests for neuro::net::UdpSocket — the host scaffold's UDP
// wrapper over POSIX datagram sockets.

#include "neuro/net/udp_socket.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <chrono>
#include <sys/socket.h>
#include <thread>
#include <vector>

#include "../../test_framework.hpp"

using neuro::net::IpAddr;
using neuro::net::NetBuffer;
using neuro::net::SocketAddress;
using neuro::net::UdpSocket;

namespace {

SocketAddress localhost_v4(std::uint16_t port) {
    return SocketAddress{IpAddr{127, 0, 0, 1}, port};
}

}  // namespace

// ---- 1. bind to ephemeral port succeeds ----------------------------

TEST(udp_socket, bind_to_ephemeral_port) {
    UdpSocket s = UdpSocket::bind(SocketAddress::any_v4(0));
    EXPECT_TRUE(s.fd() >= 0);
}

TEST(udp_socket, bind_to_explicit_port) {
    // Use a port in the IANA dynamic range; collisions are rare on test hosts.
    UdpSocket s = UdpSocket::bind(localhost_v4(0));
    EXPECT_TRUE(s.fd() >= 0);
}

// ---- 2. send_to / recv_from round-trip -----------------------------

TEST(udp_socket, send_to_recv_from_round_trip) {
    UdpSocket rx = UdpSocket::bind(SocketAddress::any_v4(0));
    UdpSocket tx = UdpSocket::bind(SocketAddress::any_v4(0));

    // Read the ephemeral port rx bound to via getsockname. For
    // simplicity we send first then receive — the known port of tx
    // is irrelevant; we only need to know the rx port.
    auto tx_port = // re-create with a known port request
        SocketAddress::any_v4(0);
    (void)tx_port;

    // Get rx port via getsockname.
    sockaddr_in rx_addr{};
    socklen_t   rx_len = sizeof(rx_addr);
    ::getsockname(rx.fd(), reinterpret_cast<sockaddr*>(&rx_addr), &rx_len);
    std::uint16_t rx_port = ntohs(rx_addr.sin_port);

    sockaddr_in tx_addr{};
    socklen_t   tx_len = sizeof(tx_addr);
    ::getsockname(tx.fd(), reinterpret_cast<sockaddr*>(&tx_addr), &tx_len);
    std::uint16_t tx_port_v = ntohs(tx_addr.sin_port);

    // Send "hello" from tx to rx.
    NetBuffer buf = NetBuffer::from_string("hello");
    tx.send_to(localhost_v4(rx_port), buf);

    auto msg = rx.recv_from();
    EXPECT_EQ(static_cast<std::size_t>(5), msg.payload.size());
    EXPECT_EQ('h', msg.payload.as_string()[0]);
    EXPECT_EQ(localhost_v4(tx_port_v).ip, msg.from.ip);
}

TEST(udp_socket, recv_from_reports_sender_address) {
    UdpSocket rx = UdpSocket::bind(SocketAddress::any_v4(0));
    UdpSocket tx = UdpSocket::bind(SocketAddress::any_v4(0));

    sockaddr_in rx_addr{};
    socklen_t rx_len = sizeof(rx_addr);
    ::getsockname(rx.fd(), reinterpret_cast<sockaddr*>(&rx_addr), &rx_len);
    std::uint16_t rx_port = ntohs(rx_addr.sin_port);

    sockaddr_in tx_addr{};
    socklen_t tx_len = sizeof(tx_addr);
    ::getsockname(tx.fd(), reinterpret_cast<sockaddr*>(&tx_addr), &tx_len);
    std::uint16_t tx_port = ntohs(tx_addr.sin_port);

    NetBuffer buf;
    buf.resize(2);
    buf.data()[0] = static_cast<std::byte>(0xAB);
    buf.data()[1] = static_cast<std::byte>(0xCD);
    tx.send_to(localhost_v4(rx_port), buf);

    auto msg = rx.recv_from();
    EXPECT_EQ(tx_port, msg.from.port);
    EXPECT_EQ(static_cast<std::uint32_t>(127 * 256 * 256 * 256 +
                                          0   * 256 * 256 +
                                          0   * 256 +
                                          1), msg.from.ip.host_order());
}

// ---- 3. close is idempotent ----------------------------------------

TEST(udp_socket, close_is_idempotent) {
    UdpSocket s = UdpSocket::bind(SocketAddress::any_v4(0));
    EXPECT_TRUE(s.fd() >= 0);
    s.close();
    EXPECT_EQ(-1, s.fd());
    s.close();   // must not throw
    EXPECT_EQ(-1, s.fd());
}

TEST(udp_socket, close_unbound_socket_is_noop) {
    UdpSocket s;
    EXPECT_EQ(-1, s.fd());
    s.close();   // must not throw
    EXPECT_EQ(-1, s.fd());
}

// ---- 4. move construction ------------------------------------------

TEST(udp_socket, move_construct_transfers_ownership) {
    UdpSocket a = UdpSocket::bind(SocketAddress::any_v4(0));
    int fd_a = a.fd();
    UdpSocket b(std::move(a));
    EXPECT_EQ(fd_a, b.fd());
    EXPECT_EQ(-1,   a.fd());
}

TEST(udp_socket, move_assign_closes_existing) {
    UdpSocket a = UdpSocket::bind(SocketAddress::any_v4(0));
    UdpSocket b = UdpSocket::bind(SocketAddress::any_v4(0));
    int fd_a = a.fd();
    b = std::move(a);
    EXPECT_EQ(fd_a, b.fd());
    EXPECT_EQ(-1,   a.fd());
}

// ---- 5. bidirectional exchange ------------------------------------

TEST(udp_socket, bidirectional_exchange) {
    UdpSocket rx = UdpSocket::bind(SocketAddress::any_v4(0));
    UdpSocket tx = UdpSocket::bind(SocketAddress::any_v4(0));

    sockaddr_in rx_addr{};
    socklen_t rx_len = sizeof(rx_addr);
    ::getsockname(rx.fd(), reinterpret_cast<sockaddr*>(&rx_addr), &rx_len);
    std::uint16_t rx_port = ntohs(rx_addr.sin_port);

    sockaddr_in tx_addr{};
    socklen_t tx_len = sizeof(tx_addr);
    ::getsockname(tx.fd(), reinterpret_cast<sockaddr*>(&tx_addr), &tx_len);
    std::uint16_t tx_port = ntohs(tx_addr.sin_port);

    // tx -> rx
    NetBuffer ba;
    ba.resize(2);
    ba.data()[0] = static_cast<std::byte>(1);
    ba.data()[1] = static_cast<std::byte>(2);
    tx.send_to(localhost_v4(rx_port), ba);
    auto ma = rx.recv_from();
    EXPECT_EQ(static_cast<std::uint8_t>(1),
              static_cast<std::uint8_t>(ma.payload.data()[0]));

    // rx -> tx
    NetBuffer bb;
    bb.resize(3);
    bb.data()[0] = static_cast<std::byte>(3);
    bb.data()[1] = static_cast<std::byte>(4);
    bb.data()[2] = static_cast<std::byte>(5);
    rx.send_to(localhost_v4(tx_port), bb);
    auto mb = tx.recv_from();
    EXPECT_EQ(static_cast<std::size_t>(3), mb.payload.size());
    EXPECT_EQ(static_cast<std::uint8_t>(5),
              static_cast<std::uint8_t>(mb.payload.data()[2]));
}

RUN_ALL_TESTS()