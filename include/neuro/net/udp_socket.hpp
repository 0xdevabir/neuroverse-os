// neuro/net/udp_socket.hpp
//
// UDP socket wrapper over POSIX datagram sockets.
//
// Per README §4.6, the host scaffold reuses the platform's network
// stack and only wraps it. The kernel net stack will replace this
// with a real in-kernel UDP implementation that goes through the
// NeuroNet capability gates.
//
// Distance-to-kernel: the surface is intentionally close to POSIX
// (recv_from / send_to) so porting the kernel implementation is
// a swap of the syscall row, not a redesign of the API.

#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "neuro/net/address.hpp"
#include "neuro/net/buffer.hpp"

namespace neuro::net {

class UdpSocket {
public:
    UdpSocket() = default;
    UdpSocket(const UdpSocket&)            = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    UdpSocket& operator=(UdpSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~UdpSocket() { close(); }

    // Bind to a local address. Use SocketAddress::any_v4(0) for an
    // ephemeral port.
    static UdpSocket bind(SocketAddress local) {
        UdpSocket s;
        s.fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (s.fd_ < 0) {
            throw std::runtime_error("UdpSocket::bind: socket() failed");
        }
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port   = htons(local.port);
        std::uint32_t ho = local.ip.host_order();
        std::memcpy(&a.sin_addr.s_addr, &ho, sizeof(a.sin_addr.s_addr));
        if (::bind(s.fd_, reinterpret_cast<sockaddr*>(&a),
                   sizeof(a)) != 0) {
            int err = errno;
            ::close(s.fd_);
            s.fd_ = -1;
            throw std::runtime_error(
                std::string("UdpSocket::bind: bind() failed: ") +
                std::strerror(err));
        }
        return s;
    }

    // Send a datagram to the given peer.
    void send_to(SocketAddress peer, const NetBuffer& buf) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port   = htons(peer.port);
        std::uint32_t ho = peer.ip.host_order();
        std::memcpy(&a.sin_addr.s_addr, &ho, sizeof(a.sin_addr.s_addr));
        ssize_t n = ::sendto(fd_, buf.data(), buf.size(), 0,
                             reinterpret_cast<sockaddr*>(&a), sizeof(a));
        if (n < 0) {
            throw std::runtime_error(
                std::string("UdpSocket::send_to: ") + std::strerror(errno));
        }
    }

    // Receive a datagram; returns the message and the sender's
    // address. Blocks until a datagram arrives.
    struct Recv {
        NetBuffer     payload;
        SocketAddress from;
    };
    Recv recv_from(std::size_t max_bytes = 64 * 1024) {
        NetBuffer buf(max_bytes);
        sockaddr_in a{};
        socklen_t len = sizeof(a);
        ssize_t n = ::recvfrom(fd_, buf.data(), buf.size(), 0,
                               reinterpret_cast<sockaddr*>(&a), &len);
        if (n < 0) {
            throw std::runtime_error(
                std::string("UdpSocket::recv_from: ") + std::strerror(errno));
        }
        buf.resize(static_cast<std::size_t>(n));

        IpAddr ip;
        std::uint32_t ho = 0;
        std::memcpy(&ho, &a.sin_addr.s_addr, sizeof(ho));
        ip = IpAddr{ho};

        return Recv{std::move(buf),
                    SocketAddress{ip, ntohs(a.sin_port)}};
    }

    [[nodiscard]] int fd() const noexcept { return fd_; }

    void close() noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

}  // namespace neuro::net
