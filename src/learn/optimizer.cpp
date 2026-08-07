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
            std::visit([&](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::vector<std::int64_t>>) {
                    s << "[";
                    for (std::size_t i = 0; i < v.size(); ++i) {
                        if (i) s << ", ";
                        s << v[i];
                    }
                    s << "]";
                } else {
                    s << v;
                }
            }, kv.second);
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

// ---- GradientOptimizer -------------------------------------------------
//
// Plain SGD with momentum:
//
//     v  ← μ * v + grad
//     π  ← π - lr * v
//
// Gradients are accumulated over multiple `observe()` calls until
// `propose()` is invoked; then a single step is taken and the
// accumulator resets. The optimizer emits a Proposal for the
// first parameter only — multi-parameter proposals are deferred
// to Phase 1.

GradientOptimizer::GradientOptimizer(double lr,
                                     std::vector<double> params,
                                     std::vector<ParamSpec> spec)
    : lr_(lr),
      params_(std::move(params)),
      spec_(std::move(spec)),
      grad_buf_(params_.size(), 0.0),
      momentum_(params_.size(), 0.0) {
    if (params_.size() != spec_.size()) {
        // Mismatched spec — drop the spec silently. Phase 1 will
        // throw here once contracts land.
        spec_.resize(params_.size());
    }
}

void GradientOptimizer::observe(const Observation& o) {
    std::lock_guard<std::mutex> g(mu_);
    if (o.key == "loss") {
        if (auto* d = std::get_if<double>(&o.value)) {
            last_loss_ = *d;
        } else if (auto* i = std::get_if<std::int64_t>(&o.value)) {
            last_loss_ = static_cast<double>(*i);
        }
        return;
    }
    if (o.key == "grad") {
        if (auto* v = std::get_if<std::vector<std::int64_t>>(&o.value)) {
            // Coerce integer grads to doubles, accumulate.
            for (std::size_t i = 0;
                 i < v->size() && i < grad_buf_.size(); ++i) {
                grad_buf_[i] += static_cast<double>((*v)[i]);
            }
            ++grad_count_;
        }
    }
}

Proposal GradientOptimizer::propose() const {
    std::lock_guard<std::mutex> g(mu_);
    if (params_.empty()) {
        return Proposal{};
    }
    if (grad_count_ == 0) {
        // No gradient yet — emit current value as a Proposal so
        // callers can see the initial state.
        Proposal p;
        p.kind = spec_.front().kind;
        p.integer_value = static_cast<std::int64_t>(params_.front());
        return p;
    }
    // Average accumulated gradient across the observations.
    const double inv = 1.0 / static_cast<double>(grad_count_);
    std::vector<double> step(grad_buf_.size(), 0.0);
    for (std::size_t i = 0; i < grad_buf_.size(); ++i) {
        const double g = grad_buf_[i] * inv;
        // Plain SGD: v ← g; π ← π - lr * v. (No momentum in this
        // minimal implementation — momentum buffer is exposed for
        // Phase 1.)
        (void)momentum_;
        params_[i] -= lr_ * g;
        step[i] = -lr_ * g;
        grad_buf_[i] = 0.0;
    }
    grad_count_ = 0;
    ++step_count_;

    Proposal p;
    p.kind = spec_.front().kind;
    p.integer_value = static_cast<std::int64_t>(params_.front());
    return p;
}

void GradientOptimizer::apply(const Proposal& p) {
    // apply() is the kernel side — on the host the optimizer just
    // records that it observed its own proposal so describe() can
    // show the loop closing.
    std::lock_guard<std::mutex> g(mu_);
    ++step_count_;
    (void)p;
}

std::string GradientOptimizer::describe() const {
    std::lock_guard<std::mutex> g(mu_);
    std::ostringstream s;
    s << "gradient_optimizer: " << params_.size()
      << " params, lr=" << lr_
      << ", loss=" << last_loss_
      << ", steps=" << step_count_
      << ", pending_grads=" << grad_count_ << "\n";
    for (std::size_t i = 0; i < params_.size(); ++i) {
        s << "  " << (i < spec_.size() ? spec_[i].name : "?")
          << " = " << params_[i] << "\n";
    }
    return s.str();
}

std::size_t GradientOptimizer::size() const noexcept {
    return params_.size();
}

double GradientOptimizer::param(std::size_t i) const {
    std::lock_guard<std::mutex> g(mu_);
    return params_.at(i);
}

const std::vector<double>& GradientOptimizer::params() const noexcept {
    return params_;
}

std::unique_ptr<Optimizer>
make_worker_count_optimizer(std::int64_t initial, double lr) {
    std::vector<double> p{static_cast<double>(initial)};
    std::vector<GradientOptimizer::ParamSpec> s{
        {"worker_count", Proposal::Kind::SetWorkerCount},
    };
    return std::make_unique<GradientOptimizer>(lr, std::move(p), std::move(s));
}

}  // namespace neuro::learn