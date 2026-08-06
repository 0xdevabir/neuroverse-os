// src/fabric/membership.cpp
//
// Cluster membership — host scaffold.
//
// On the host we only need a process-wide singleton that satisfies the
// Cluster trait. The real SWIM-style implementation (ping / ack /
// ping-req / suspicion) lands with the kernel net stack in Phase 1.

#include "neuro/fabric/membership.hpp"

#include <mutex>

namespace neuro::fabric {

namespace {

// Host-side stub. Holds a single Member (ourselves) and no peers.
class HostCluster : public Cluster {
public:
    void local(NodeId id, std::string addr) override {
        std::lock_guard<std::mutex> g(mu_);
        self_.id     = id == 0 ? 1 : id;
        self_.addr   = std::move(addr);
        self_.status = Status::Alive;
    }

    void start(Callbacks /*cb*/) override {
        std::lock_guard<std::mutex> g(mu_);
        running_ = true;
    }

    void stop() override {
        std::lock_guard<std::mutex> g(mu_);
        running_ = false;
    }

    [[nodiscard]] std::vector<Member> members() const override {
        std::lock_guard<std::mutex> g(mu_);
        if (self_.id == 0) return {};
        return {self_};
    }

    bool ping(NodeId id) override {
        std::lock_guard<std::mutex> g(mu_);
        return id == self_.id;
    }

    [[nodiscard]] NodeId self() const noexcept override {
        std::lock_guard<std::mutex> g(mu_);
        return self_.id;
    }

private:
    mutable std::mutex mu_;
    Member             self_{};
    bool               running_ = false;
};

}  // namespace

Cluster& host_cluster() {
    static HostCluster c;
    return c;
}

}  // namespace neuro::fabric
