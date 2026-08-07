// tests/integration/net_cap.cpp
//
// Z7.4 — net socket via capability.
//
// Composes net (TcpSocket::listen / connect) with sec (CapRights)
// to demonstrate the pattern: a "socket object" has a capability
// that must hold Read+Write (for connect/accept) before the call
// succeeds. The host scaffold keeps a thin wrapper that checks
// the cap before delegating to TcpSocket.

#include "tests/test_framework.hpp"

#include "neuro/net/address.hpp"
#include "neuro/net/tcp_socket.hpp"
#include "neuro/sec/cap_ops.hpp"
#include "neuro/sec/cap_space.hpp"
#include "neuro/sec/epoch.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using neuro::core::CapRight;
using neuro::net::IpAddr;
using neuro::net::SocketAddress;
using neuro::net::TcpSocket;
using neuro::sec::CapabilitySpace;
using neuro::sec::CapEpoch;
using neuro::sec::CapOps;

namespace {

constexpr std::uint64_t SOCKET_OBJECT_ID = 0x50CC500ULL;

// Host-side mock of a kernel "cap-gated listen". The cap must hold
// Read+Write for listen() to be permitted.
TcpSocket listen_with_cap(CapabilitySpace& space, CapEpoch& epoch,
                          std::uint64_t cap_handle,
                          const SocketAddress& addr) {
    auto cap = CapOps::resolve(space, epoch, cap_handle,
                                CapRight::Read | CapRight::Write);
    if (!cap) {
        throw std::runtime_error("listen_with_cap: capability missing "
                                  "required rights");
    }
    return TcpSocket::listen(addr);
}

TcpSocket connect_with_cap(CapabilitySpace& space, CapEpoch& epoch,
                            std::uint64_t cap_handle,
                            const SocketAddress& addr) {
    auto cap = CapOps::resolve(space, epoch, cap_handle,
                                CapRight::Write);
    if (!cap) {
        throw std::runtime_error("connect_with_cap: capability missing "
                                  "Write right");
    }
    return TcpSocket::connect(addr);
}

}  // namespace

TEST(net_cap, listen_with_required_caps_succeeds) {
    CapabilitySpace space;
    CapEpoch        epoch;
    auto handle = CapOps::mint(space, epoch, SOCKET_OBJECT_ID,
                                CapRight::Read | CapRight::Write, 1);

    auto server = listen_with_cap(space, epoch, handle,
                                   SocketAddress::loopback(0));
    EXPECT_TRUE(server.fd() >= 0);
    server.close();
}

TEST(net_cap, listen_without_caps_throws) {
    CapabilitySpace space;
    CapEpoch        epoch;
    // Cap holds only Read — listen needs Read+Write.
    auto handle = CapOps::mint(space, epoch, SOCKET_OBJECT_ID,
                                CapRight::Read, 1);

    bool threw = false;
    try {
        (void)listen_with_cap(space, epoch, handle,
                               SocketAddress::loopback(0));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

TEST(net_cap, connect_requires_write_right) {
    // Set up a real listening socket.
    auto server = TcpSocket::listen(SocketAddress::loopback(0));
    sockaddr_in addr{};
    socklen_t alen = sizeof(addr);
    ::getsockname(server.fd(),
                  reinterpret_cast<sockaddr*>(&addr), &alen);
    auto port = ntohs(addr.sin_port);

    CapabilitySpace space;
    CapEpoch        epoch;
    auto handle = CapOps::mint(space, epoch, SOCKET_OBJECT_ID,
                                CapRight::Write, 1);

    std::atomic<bool> accepted{false};
    std::thread accept_thread([&] {
        try {
            (void)server.accept();
            accepted.store(true);
        } catch (...) {}
    });

    auto client = connect_with_cap(space, epoch, handle,
                                    SocketAddress::loopback(port));
    EXPECT_TRUE(client.fd() >= 0);
    client.close();
    accept_thread.join();
    EXPECT_TRUE(accepted.load());
    server.close();
}

TEST(net_cap, revoke_invalidates_socket_cap) {
    CapabilitySpace space;
    CapEpoch        epoch;
    auto handle = CapOps::mint(space, epoch, SOCKET_OBJECT_ID,
                                CapRight::Read | CapRight::Write, 1);

    // Initially OK.
    {
        auto s = listen_with_cap(space, epoch, handle,
                                  SocketAddress::loopback(0));
        EXPECT_TRUE(s.fd() >= 0);
        s.close();
    }

    CapOps::revoke(space, epoch);

    bool threw = false;
    try {
        (void)listen_with_cap(space, epoch, handle,
                               SocketAddress::loopback(0));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    EXPECT_TRUE(threw);
}

RUN_ALL_TESTS()