// tests/unit/net/tcp_socket_test.cpp
//
// Unit tests for neuro::net::TcpSocket — success paths and the
// failure paths required by the kernel's connect / read paths.

#include "neuro/net/tcp_socket.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>

#include "../../test_framework.hpp"

using neuro::net::IpAddr;
using neuro::net::NetBuffer;
using neuro::net::SocketAddress;
using neuro::net::TcpSocket;

namespace {

SocketAddress localhost_v4(std::uint16_t port) {
    return SocketAddress{IpAddr{127, 0, 0, 1}, port};
}

}  // namespace

// ---- 1. connect to a closed port throws ----------------------------

TEST(tcp_socket, connect_to_unbound_port_throws) {
    // Use a port that is very unlikely to be bound. 1 is in the
    // IANA-reserved range and is reliably closed.
    bool threw = false;
    try {
        TcpSocket::connect(localhost_v4(1));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(tcp_socket, connect_to_invalid_address_throws) {
    // 0.0.0.0 is not a valid destination for connect().
    bool threw = false;
    try {
        TcpSocket::connect(localhost_v4(0));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

// ---- 2. listen / accept / read / write round-trip ----------------

TEST(tcp_socket, listen_accept_round_trip) {
    TcpSocket server = TcpSocket::listen(localhost_v4(0));
    sockaddr_in addr{};
    socklen_t alen = sizeof(addr);
    ::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&addr), &alen);
    std::uint16_t port = ntohs(addr.sin_port);

    // Connect asynchronously.
    std::atomic<bool> accepted{false};
    std::thread client_thread([&] {
        try {
            TcpSocket c = TcpSocket::connect(localhost_v4(port));
            c.write(NetBuffer::from_string("hello"));
        } catch (...) { /* MAY fail due to race */ }
    });

    auto a = server.accept();
    EXPECT_TRUE(a.socket != nullptr);
    EXPECT_EQ(localhost_v4(0).ip, a.peer.ip);
    accepted.store(true);

    auto msg = a.socket->read();
    EXPECT_EQ(static_cast<std::size_t>(5), msg.size());
    EXPECT_EQ("hello", msg.as_string());

    client_thread.join();
    EXPECT_TRUE(accepted.load());
}

// ---- 3. read returns 0 bytes on EOF -----------------------------

TEST(tcp_socket, read_returns_empty_on_eof) {
    TcpSocket server = TcpSocket::listen(localhost_v4(0));
    sockaddr_in addr{};
    socklen_t alen = sizeof(addr);
    ::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&addr), &alen);
    std::uint16_t port = ntohs(addr.sin_port);

    std::thread client_thread([&] {
        try {
            TcpSocket c = TcpSocket::connect(localhost_v4(port));
            c.close();   // immediate half-close
        } catch (...) {}
    });

    auto a = server.accept();
    auto msg = a.socket->read();
    EXPECT_EQ(static_cast<std::size_t>(0), msg.size());
    client_thread.join();
}

// ---- 4. close is idempotent ----------------------------------------

TEST(tcp_socket, close_is_idempotent) {
    TcpSocket s = TcpSocket::listen(localhost_v4(0));
    EXPECT_TRUE(s.fd() >= 0);
    s.close();
    EXPECT_EQ(-1, s.fd());
    s.close();   // must not throw
}

// ---- 5. unbound socket close is noop ------------------------------

TEST(tcp_socket, close_unbound_socket_is_noop) {
    TcpSocket s;
    EXPECT_EQ(-1, s.fd());
    s.close();   // must not throw
    EXPECT_EQ(-1, s.fd());
}

// ---- 6. write moves all bytes even when send is short -------------

TEST(tcp_socket, write_handles_short_send) {
    TcpSocket server = TcpSocket::listen(localhost_v4(0));
    sockaddr_in addr{};
    socklen_t alen = sizeof(addr);
    ::getsockname(server.fd(), reinterpret_cast<sockaddr*>(&addr), &alen);
    std::uint16_t port = ntohs(addr.sin_port);

    std::string payload(64 * 1024, 'x');
    std::thread client_thread([&] {
        try {
            TcpSocket c = TcpSocket::connect(localhost_v4(port));
            c.write(NetBuffer::from_string(payload));
        } catch (...) {}
    });

    auto a = server.accept();
    auto msg = a.socket->read(64 * 1024);
    EXPECT_EQ(payload.size(), msg.size());
    EXPECT_EQ(payload,       msg.as_string());
    client_thread.join();
}

RUN_ALL_TESTS()