// neuro/net/tcp_socket.hpp
//
// TCP socket wrapper over POSIX stream sockets.
//
// Per README §4.6, the host scaffold reuses the platform's network
// stack. The kernel net stack replaces the syscall row with an
// in-kernel TCP path that goes through the NeuroNet capability
// gates; the public API stays stable.
//
// On the host scaffold we keep the surface close to POSIX (read /
// write / accept) so porting to the kernel is a swap, not a
// redesign. Async coroutine read/write is a future phase that
// integrates with neuro::sched::ws::Scheduler.

#pragma once

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "neuro/net/address.hpp"
#include "neuro/net/buffer.hpp"

namespace neuro::net {

class TcpSocket;  // forward

// Defined out-of-class so the Accepted struct can hold a fully
// complete TcpSocket value. We use a unique_ptr because the
// struct is defined before the class body is complete.
struct Accepted {
    std::unique_ptr<TcpSocket> socket;
    SocketAddress              peer;
};

class TcpSocket {
public:
    TcpSocket() = default;
    TcpSocket(const TcpSocket&)            = delete;
    TcpSocket& operator=(const TcpSocket&) = delete;

    TcpSocket(TcpSocket&& other) noexcept : fd_(other.fd_) {
        other.fd_ = -1;
    }
    TcpSocket& operator=(TcpSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    ~TcpSocket() { close(); }

    // Open a connected socket to peer. On the host this is a thin
    // wrapper around socket() + connect().
    static TcpSocket connect(SocketAddress peer) {
        TcpSocket s;
        s.fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s.fd_ < 0) {
            throw std::runtime_error("TcpSocket::connect: socket() failed");
        }
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port   = htons(peer.port);
        std::uint32_t ho = peer.ip.host_order();
        std::memcpy(&a.sin_addr.s_addr, &ho, sizeof(a.sin_addr.s_addr));
        if (::connect(s.fd_, reinterpret_cast<sockaddr*>(&a),
                      sizeof(a)) != 0) {
            int err = errno;
            ::close(s.fd_);
            s.fd_ = -1;
            throw std::runtime_error(
                std::string("TcpSocket::connect: connect() failed: ") +
                std::strerror(err));
        }
        return s;
    }

    // Listen for incoming connections on a local address.
    static TcpSocket listen(SocketAddress local, int backlog = 16) {
        TcpSocket s;
        s.fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (s.fd_ < 0) {
            throw std::runtime_error("TcpSocket::listen: socket() failed");
        }
        int one = 1;
        ::setsockopt(s.fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

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
                std::string("TcpSocket::listen: bind() failed: ") +
                std::strerror(err));
        }
        if (::listen(s.fd_, backlog) != 0) {
            int err = errno;
            ::close(s.fd_);
            s.fd_ = -1;
            throw std::runtime_error(
                std::string("TcpSocket::listen: listen() failed: ") +
                std::strerror(err));
        }
        return s;
    }

    // Accept one connection. Returns {new socket, peer addr}.
    Accepted accept();

    // Read up to buf.size() bytes, returning a buffer of the
    // exact bytes received.
    NetBuffer read(std::size_t max_bytes = 64 * 1024) {
        NetBuffer buf(max_bytes);
        ssize_t n = ::recv(fd_, buf.data(), buf.size(), 0);
        if (n < 0) {
            throw std::runtime_error(
                std::string("TcpSocket::read: ") + std::strerror(errno));
        }
        if (n == 0) {
            // Peer closed — return empty buffer to signal EOF.
            buf.clear();
            return buf;
        }
        buf.resize(static_cast<std::size_t>(n));
        return buf;
    }

    // Write all bytes; loop over short writes.
    void write(const NetBuffer& buf) {
        std::size_t off = 0;
        while (off < buf.size()) {
            ssize_t n = ::send(fd_, buf.data() + off, buf.size() - off, 0);
            if (n < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error(
                    std::string("TcpSocket::write: ") + std::strerror(errno));
            }
            off += static_cast<std::size_t>(n);
        }
    }

    void write(std::string_view s) {
        NetBuffer buf(reinterpret_cast<const std::byte*>(s.data()),
                      s.size());
        write(buf);
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

inline Accepted TcpSocket::accept() {
    sockaddr_in a{};
    socklen_t len = sizeof(a);
    int new_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&a), &len);
    if (new_fd < 0) {
        throw std::runtime_error(
            std::string("TcpSocket::accept: ") + std::strerror(errno));
    }
    auto ns = std::make_unique<TcpSocket>();
    ns->fd_ = new_fd;
    std::uint32_t ho = 0;
    std::memcpy(&ho, &a.sin_addr.s_addr, sizeof(ho));
    IpAddr ip{ho};
    return Accepted{std::move(ns),
                    SocketAddress{ip, ntohs(a.sin_port)}};
}

}  // namespace neuro::net
