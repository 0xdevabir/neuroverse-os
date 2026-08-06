// neuro/fabric/membership.hpp
//
// Cluster membership skeleton (NeuroFabric, README §4.11).
//
// The cluster fabric provides node discovery, liveness, and
// gossiped metadata across the machines that run NeuroVerse.
// On the host scaffold we expose the trait surface (member id,
// incarnation, status, peer set, callbacks) and stub out the
// SWIM-style protocol implementation. The real protocol —
// ping / ack / ping-req + suspicion — lands with the kernel
// net stack in Phase 1.

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace neuro::fabric {

using NodeId = std::uint64_t;

// Lifecycle states a cluster member can be in.
enum class Status : std::uint8_t {
    Alive,
    Suspect,   // missed recent ping; ask others to confirm
    Dead,      // confirmed unreachable
    Leaving,   // graceful shutdown in progress
    Left,      // announced departure
};

// One member of the cluster.
struct Member {
    NodeId    id           = 0;
    Status    status       = Status::Alive;
    std::uint64_t incarnation = 1;     // bumped on revive
    std::string addr;                  // "host:port" hint
    std::chrono::steady_clock::time_point last_seen{};
};

// Callbacks invoked on membership changes. The fabric keeps
// ownership of the callbacks; users register them once at start.
struct Callbacks {
    std::function<void(const Member&)>            on_join;
    std::function<void(const Member&)>            on_leave;
    std::function<void(const Member&)>            on_suspect;
    std::function<void(const Member&)>            on_revive;
};

// Cluster membership handle. One process holds one Cluster;
// the kernel implementation backs it with the SWIM protocol.
class Cluster {
public:
    Cluster()                                  = default;
    Cluster(const Cluster&)                    = delete;
    Cluster& operator=(const Cluster&)         = delete;
    virtual ~Cluster()                         = default;

    // Configure the local node. id=0 means "pick one".
    virtual void local(NodeId id, std::string addr) = 0;

    // Start the gossip background work.
    virtual void start(Callbacks cb) = 0;

    // Stop the gossip background work and announce Leaving.
    virtual void stop() = 0;

    // Snapshot of the current view.
    [[nodiscard]] virtual std::vector<Member>
        members() const = 0;

    // Direct probe (synchronous). Used by tests and tools.
    virtual bool ping(NodeId id) = 0;

    // Helper: is this node us?
    [[nodiscard]] virtual NodeId self() const noexcept = 0;
};

// Singleton factory: one Cluster per process.
Cluster& host_cluster();

}  // namespace neuro::fabric