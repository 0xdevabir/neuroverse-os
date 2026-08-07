// src/fabric/membership.cpp
//
// Cluster membership — host scaffold (S1).
//
// On the host we provide a real SWIM-style gossip state machine:
//   - members can be Alive / Suspect / Dead / Leaving / Left.
//   - a peer that's missed `ping_timeout` advances to Suspect.
//   - a Suspect that misses another `ping_timeout` advances to Dead.
//   - callbacks fire on every transition.
//   - mark() lets operators (and tests) drive transitions
//     manually; advance() simulates elapsed time without needing
//     a background thread.
//
// The kernel implementation will replace the synthetic clock
// with real UDP pings but the state machine is identical.

#include "neuro/fabric/membership.hpp"

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace neuro::fabric {

class HostCluster : public Cluster {
public:
    void configure(std::chrono::milliseconds probe_interval,
                   std::chrono::milliseconds ping_timeout) override {
        std::lock_guard<std::mutex> g(mu_);
        probe_interval_ = probe_interval;
        ping_timeout_   = ping_timeout;
    }

    void local(NodeId id, std::string addr) override {
        std::lock_guard<std::mutex> g(mu_);
        self_.id       = id == 0 ? 1 : id;
        self_.addr     = std::move(addr);
        self_.status   = Status::Alive;
        self_.last_seen = now();
        peers_[self_.id] = self_;
    }

    void start(Callbacks cb, std::vector<Member> initial_peers) override {
        std::lock_guard<std::mutex> g(mu_);
        cb_ = std::move(cb);
        running_ = true;
        for (auto& p : initial_peers) {
            if (p.id == self_.id) continue;
            p.last_seen = now();
            peers_[p.id] = p;
            if (cb_.on_join) cb_.on_join(peers_[p.id]);
        }
    }

    void stop() override {
        std::lock_guard<std::mutex> g(mu_);
        if (self_.id != 0 && cb_.on_leave && running_) {
            self_.status = Status::Left;
            cb_.on_leave(self_);
        }
        running_ = false;
    }

    [[nodiscard]] std::vector<Member> members() const override {
        std::lock_guard<std::mutex> g(mu_);
        std::vector<Member> out;
        out.reserve(peers_.size());
        for (auto& kv : peers_) out.push_back(kv.second);
        return out;
    }

    bool ping(NodeId id) override {
        std::lock_guard<std::mutex> g(mu_);
        auto it = peers_.find(id);
        if (it == peers_.end()) return false;
        it->second.last_seen = now();
        if (it->second.status == Status::Suspect ||
            it->second.status == Status::Dead) {
            // Ping from a peer effectively revives it.
            Status old = it->second.status;
            it->second.status = Status::Alive;
            it->second.incarnation++;
            if (old != Status::Alive && cb_.on_revive) {
                cb_.on_revive(it->second);
            }
        }
        return true;
    }

    void mark(NodeId id, Status s) override {
        std::lock_guard<std::mutex> g(mu_);
        auto it = peers_.find(id);
        if (it == peers_.end()) return;
        Status old = it->second.status;
        if (old == s) return;
        it->second.status = s;
        if (s == Status::Alive && cb_.on_revive) {
            it->second.incarnation++;
            cb_.on_revive(it->second);
        } else if (s == Status::Suspect && cb_.on_suspect) {
            cb_.on_suspect(it->second);
        } else if (s == Status::Dead && cb_.on_leave) {
            cb_.on_leave(it->second);
        }
    }

    void advance(std::chrono::milliseconds dt) override {
        std::lock_guard<std::mutex> g(mu_);
        if (!running_) return;
        // Apply the time step first so the timeout check below
        // sees the updated offset.
        now_offset_ += dt;
        // Walk every peer; apply timeouts. We process deaths in
        // a separate pass so we can call cb_.on_leave without
        // mutating the map mid-iteration.
        std::vector<std::pair<NodeId, Status>> transitions;
        for (auto& kv : peers_) {
            if (kv.first == self_.id) continue;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now() - kv.second.last_seen);
            // The timeout clock is anchored on `last_seen`; tests
            // advance the wall clock via now_offset_ instead.
            elapsed += std::chrono::duration_cast<std::chrono::milliseconds>(
                now_offset_);
            if (elapsed >= ping_timeout_ * 2 &&
                kv.second.status == Status::Suspect) {
                transitions.emplace_back(kv.first, Status::Dead);
            } else if (elapsed >= ping_timeout_ &&
                       kv.second.status == Status::Alive) {
                transitions.emplace_back(kv.first, Status::Suspect);
            }
        }
        for (auto& [id, target] : transitions) {
            auto it = peers_.find(id);
            if (it == peers_.end()) continue;
            Status old = it->second.status;
            if (old == target) continue;
            it->second.status = target;
            if (target == Status::Suspect && cb_.on_suspect) {
                cb_.on_suspect(it->second);
            } else if (target == Status::Dead && cb_.on_leave) {
                cb_.on_leave(it->second);
            }
        }
    }

    [[nodiscard]] NodeId self() const noexcept override {
        std::lock_guard<std::mutex> g(mu_);
        return self_.id;
    }

private:
    static std::chrono::steady_clock::time_point now() {
        return std::chrono::steady_clock::now();
    }

    mutable std::mutex                                     mu_;
    Member                                                 self_{};
    std::unordered_map<NodeId, Member>                     peers_;
    Callbacks                                              cb_;
    bool                                                   running_ = false;
    std::chrono::milliseconds                              probe_interval_{500};
    std::chrono::milliseconds                              ping_timeout_{1500};
    std::chrono::milliseconds                              now_offset_{0};
};

Cluster& host_cluster() {
    static HostCluster c;
    return c;
}

std::unique_ptr<Cluster> make_test_cluster() {
    return std::make_unique<HostCluster>();
}

}  // namespace neuro::fabric
