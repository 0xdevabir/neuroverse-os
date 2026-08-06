// neuro/net/dns.hpp
//
// DNS resolver stub.
//
// Per README §4.6, NeuroNet needs a name-resolution primitive that
// eventually lives inside the kernel net stack (capability-gated,
// low-latency, with the answer cache as a NeuroMem region). On the
// host scaffold we delegate to POSIX getaddrinfo() so the rest of
// the stack can be developed and tested without a resolver-level
// stub masquerading as a real DNS path.
//
// The kernel implementation drops the getaddrinfo() call and runs
// its own resolver; the public API stays identical.

#pragma once

#include <arpa/inet.h>
#include <cstdint>
#include <cstring>
#include <netdb.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "neuro/net/address.hpp"

namespace neuro::net {

class Dns {
public:
    // Resolve a hostname to one or more IPv4 addresses. Throws
    // std::runtime_error if resolution fails.
    static std::vector<IpAddr> resolve_v4(std::string_view host) {
        // getaddrinfo needs a NUL-terminated string.
        std::string host_str{host};

        addrinfo hints{};
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        int rc = ::getaddrinfo(host_str.c_str(), nullptr, &hints, &res);
        if (rc != 0) {
            throw std::runtime_error(
                std::string("Dns::resolve_v4: ") + ::gai_strerror(rc));
        }

        std::vector<IpAddr> out;
        for (addrinfo* p = res; p != nullptr; p = p->ai_next) {
            if (p->ai_family != AF_INET) continue;
            const sockaddr_in* sa =
                reinterpret_cast<const sockaddr_in*>(p->ai_addr);
            // sin_addr.s_addr is in network byte order; convert to host.
            std::uint32_t ho = ntohl(sa->sin_addr.s_addr);
            out.push_back(IpAddr{ho});
        }
        ::freeaddrinfo(res);
        return out;
    }

    // Convenience: return the first IPv4 address, or IpAddr{} on
    // failure. Useful for tests and the bootstrap config.
    static IpAddr resolve_one_v4(std::string_view host) {
        auto v = resolve_v4(host);
        if (v.empty()) return IpAddr{};
        return v.front();
    }
};

}  // namespace neuro::net
