// src/learn/optimizer.cpp
//
// Optimizer — host scaffold. The HostOptimizer keeps a small
// observation log and emits a "SetWorkerCount" proposal whose
// integer value equals the most recent "worker_count" observation
// (or a default if none has been seen). The real learned model
// lands in Phase 1.

#include "neuro/learn/optimizer.hpp"

#include <mutex>
#include <sstream>

namespace neuro::learn {

namespace {

class HostOptimizer : public Optimizer {
public:
    void observe(const Observation& o) override {
        std::lock_guard<std::mutex> g(mu_);
        last_[o.key] = o.value;
    }

    [[nodiscard]] Proposal propose() const override {
        std::lock_guard<std::mutex> g(mu_);
        auto it = last_.find("worker_count");
        if (it == last_.end()) return Proposal{};
        std::int64_t n = 0;
        if (std::holds_alternative<std::int64_t>(it->second)) {
            n = std::get<std::int64_t>(it->second);
        } else if (std::holds_alternative<double>(it->second)) {
            n = static_cast<std::int64_t>(std::get<double>(it->second));
        }
        return make_set_workers(n);
    }

    [[nodiscard]] std::string describe() const override {
        std::lock_guard<std::mutex> g(mu_);
        std::ostringstream s;
        s << "host_optimizer: " << last_.size() << " observations\n";
        for (auto& kv : last_) {
            s << "  " << kv.first << " = ";
            std::visit([&](auto&& v) { s << v; }, kv.second);
            s << "\n";
        }
        return s.str();
    }

private:
    mutable std::mutex                                  mu_;
    std::unordered_map<std::string, ObservationValue>   last_;
};

}  // namespace

Optimizer& host_optimizer() {
    static HostOptimizer o;
    return o;
}

}  // namespace neuro::learn