// tests/integration/net_echo.cpp
//
// TCP echo server test.
//
// Spins up a listener on an ephemeral loopback port, accepts one
// connection on a worker thread, echoes every byte back, and
// verifies the client receives the same payload it sent.
//
// Per README §4.6 the host scaffold reuses POSIX; the kernel net
// stack replaces the syscall row with the capability-gated in-
// kernel path. The test exercises only the public wrapper API, so
// it survives the swap unchanged.
//
// The test also confirms that:
//   - Dns::resolve_one_v4("localhost") returns a sane IPv4 address
//     on the host (used as a sanity check, not as the actual peer
//     for the loopback socket — we connect to 127.0.0.1 directly).
//   - IpAddr::parse("127.0.0.1") round-trips through to_string().
//   - A non-trivial multi-chunk write/read round-trips intact
//     across the loopback interface.

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "tests/test_framework.hpp"

#include "neuro/net/address.hpp"
#include "neuro/net/dns.hpp"
#include "neuro/net/tcp_socket.hpp"

using neuro::net::Accepted;
using neuro::net::Dns;
using neuro::net::IpAddr;
using neuro::net::SocketAddress;
using neuro::net::TcpSocket;

namespace {

// Pull the actual port the OS assigned to a TcpSocket bound to
// port 0. Uses getsockname.
std::uint16_t bound_port(const TcpSocket& s) {
    sockaddr_in a{};
    socklen_t len = sizeof(a);
    if (::getsockname(s.fd(), reinterpret_cast<sockaddr*>(&a), &len) != 0) {
        throw std::runtime_error("getsockname failed");
    }
    return ntohs(a.sin_port);
}

void run_echo_server(TcpSocket listener, std::atomic<bool>* stop) {
    // Make the listener non-blocking so accept() returns EAGAIN
    // and we can re-check the stop flag periodically.
    int flags = ::fcntl(listener.fd(), F_GETFL, 0);
    ::fcntl(listener.fd(), F_SETFL, flags | O_NONBLOCK);

    while (!stop->load(std::memory_order_acquire)) {
        Accepted conn;
        try {
            conn = listener.accept();
        } catch (const std::runtime_error&) {
            // EAGAIN — no incoming connection ready. Poll briefly
            // before re-checking stop.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        try {
            for (;;) {
                auto buf = conn.socket->read();
                if (buf.empty()) break;  // peer closed
                conn.socket->write(buf);
            }
        } catch (...) {
            // peer hung up or some other transient error — close
            // and keep accepting.
        }
    }
}

}  // namespace

TEST(net, parse_and_to_string_round_trip) {
    IpAddr a = IpAddr::parse("127.0.0.1");
    EXPECT_EQ(a.to_string(), "127.0.0.1");
    IpAddr b = IpAddr::parse("192.0.2.42");
    EXPECT_EQ(b.to_string(), "192.0.2.42");
    EXPECT_TRUE(a != b);
    IpAddr expected{127, 0, 0, 1};
    EXPECT_EQ(a, expected);
}

TEST(net, dns_resolves_localhost) {
    IpAddr ip = Dns::resolve_one_v4("localhost");
    // On every reasonable host stack localhost is 127.0.0.1.
    IpAddr expected{127, 0, 0, 1};
    EXPECT_EQ(ip, expected);
}

TEST(net, tcp_echo_round_trip) {
    TcpSocket listener = TcpSocket::listen(SocketAddress::any_v4(0));
    std::uint16_t port = bound_port(listener);

    std::atomic<bool> stop{false};
    std::thread server([&] { run_echo_server(std::move(listener), &stop); });

    TcpSocket client = TcpSocket::connect(SocketAddress::loopback(port));

    constexpr const char* kMsg =
        "Hello, NeuroVerse OS echo server!";
    client.write(std::string_view{kMsg});

    // Read everything the server echoes back. The server writes
    // exactly what it reads, so we expect len bytes.
    std::vector<char> got;
    got.reserve(std::strlen(kMsg));
    while (got.size() < std::strlen(kMsg)) {
        auto chunk = client.read();
        if (chunk.empty()) break;
        got.insert(got.end(),
                   reinterpret_cast<const char*>(chunk.data()),
                   reinterpret_cast<const char*>(chunk.data()) + chunk.size());
    }
    EXPECT_EQ(got.size(), std::strlen(kMsg));
    EXPECT_EQ(std::string(got.begin(), got.end()), std::string{kMsg});

    // Close the client so the server's read() returns EOF and the
    // server thread moves on to the next accept().
    client.close();
    stop.store(true, std::memory_order_release);
    server.join();
}

TEST(net, tcp_multi_chunk_round_trip) {
    // Send a larger payload in chunks; ensure the echo server
    // returns the full concatenation without reordering.
    TcpSocket listener = TcpSocket::listen(SocketAddress::any_v4(0));
    std::uint16_t port = bound_port(listener);

    std::atomic<bool> stop{false};
    std::thread server([&] { run_echo_server(std::move(listener), &stop); });

    TcpSocket client = TcpSocket::connect(SocketAddress::loopback(port));

    std::string expected;
    for (int i = 0; i < 8; ++i) {
        std::string chunk = "chunk-" + std::to_string(i) + ";";
        expected += chunk;
        client.write(std::string_view{chunk});
    }

    std::string got;
    while (got.size() < expected.size()) {
        auto chunk = client.read();
        if (chunk.empty()) break;
        got.append(reinterpret_cast<const char*>(chunk.data()), chunk.size());
    }
    EXPECT_EQ(got, expected);

    client.close();
    stop.store(true, std::memory_order_release);
    server.join();
}

RUN_ALL_TESTS()
