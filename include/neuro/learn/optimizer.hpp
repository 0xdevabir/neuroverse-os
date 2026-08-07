// neuro/learn/optimizer.hpp
//
// Optimizer skeleton (NeuroLearn, README §4.16).
//
// Per README §4.16 the OS is self-evolving: a learned optimizer
// observes runtime telemetry (queue depth, IPC latency, scheduler
// load) and proposes parameter changes (worker count, scheduling
// quantum, cache prefetch distance). On the host we expose the
// trait surface (Observation, Proposal, Optimizer); the real
// learned model + safe-application protocol lands in Phase 1.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace neuro::learn {

// One observation fed to the optimizer: a key/value snapshot of
// telemetry at a point in time. Values can be int, double, a
// string label, or a vector of ints (gradient vectors).
using ObservationValue = std::variant<std::int64_t,
                                      double,
                                      std::string,
                                      std::vector<std::int64_t>>;

struct Observation {
    std::string                                  key;
    ObservationValue                             value;
    std::int64_t                                 timestamp_ns = 0;
};

// One optimizer proposal: change a knob. Safety is enforced by the
// kernel in Phase 1; here we expose the surface only.
struct Proposal {
    enum class Kind : std::uint8_t {
        SetWorkerCount,        // change worker thread count
        SetSchedulingQuantum,  // change scheduling quantum (µs)
        SetPrefetchDistance,   // change cache prefetch hint (pages)
        ToggleFeature,         // enable/disable a named feature
    };

    Kind         kind = Kind::SetWorkerCount;
    std::string  feature_name;       // for ToggleFeature
    std::int64_t integer_value = 0;  // for the first three kinds
};

// Optimizer trait. Concrete implementations observe telemetry,
// decide on a proposal, and emit it for the kernel to apply.
class Optimizer {
public:
    Optimizer()                       = default;
    Optimizer(const Optimizer&)       = delete;
    Optimizer& operator=(const Optimizer&) = delete;
    virtual ~Optimizer()              = default;

    // Feed one observation to the optimizer.
    virtual void observe(const Observation& o) = 0;

    // Feed many observations in one call.
    virtual void observe_many(const std::vector<Observation>& os) {
        for (auto& o : os) observe(o);
    }

    // Produce a proposal given the current observation window.
    // Returning a default-constructed Proposal means "no change".
    [[nodiscard]] virtual Proposal propose() const = 0;

    // Apply a proposal locally (no-op on the host; real impl in
    // Phase 1 negotiates with the kernel scheduler + driver bus).
    virtual void apply(const Proposal& /*p*/) {}

    // Diagnostic snapshot: human-readable state.
    [[nodiscard]] virtual std::string describe() const = 0;

    // Parameter count. The host stub returns 0; gradient-style
    // optimizers return the size of their parameter vector.
    [[nodiscard]] virtual std::size_t size() const noexcept { return 0; }
};

// Singleton factory: one Optimizer per process.
Optimizer& host_optimizer();

// Helper: build a Proposal quickly.
inline Proposal make_set_workers(std::int64_t n) {
    Proposal p;
    p.kind = Proposal::Kind::SetWorkerCount;
    p.integer_value = n;
    return p;
}

inline Proposal make_set_quantum_us(std::int64_t q) {
    Proposal p;
    p.kind = Proposal::Kind::SetSchedulingQuantum;
    p.integer_value = q;
    return p;
}

inline Proposal make_toggle(std::string feature, bool enable) {
    Proposal p;
    p.kind = Proposal::Kind::ToggleFeature;
    p.feature_name = std::move(feature);
    p.integer_value = enable ? 1 : 0;
    return p;
}

// ---- Gradient-descent optimizer (real implementation) -------------------
//
// The host skeleton supports a simple last-value-wins optimizer
// (see HostOptimizer in src/learn/optimizer.cpp). For the parts of
// the system that actually fit a model on telemetry, we also
// expose a gradient-descent optimizer that maintains a parameter
// vector π and updates it as:
//
//     π ← π - lr * grad(loss)
//
// Each "observation" is one of two kinds:
//   - "loss"  : φ.value is a double (the loss at the current π)
//   - "grad"  : φ.value is an std::vector<std::int64_t> of length
//               grads.size(); the optimizer averages contributions
//               from multiple grad observations before stepping.
//
// The optimizer emits a Proposal that nudges the kernel knob
// associated with the parameter named in `param_name`. The
// association is the caller's responsibility — `param_name` is
// just a label so tests can match proposals back to the
// underlying parameter.
//
// Real implementations in Phase 1 will swap the inner update rule
// for an Adam/RMSProp variant and persist the parameter vector
// across boot, but the API surface is what callers will code
// against.

class GradientOptimizer : public Optimizer {
public:
    // lr      : learning rate (per-step scalar).
    // params  : initial parameter vector.
    // params  : list of (param_name, kind) tuples that the
    //           optimizer will emit proposals for. The
    //           `param_name` is the *display label*; the
    //           parameter's index in `params` is what the
    //           optimizer tracks internally.
    struct ParamSpec {
        std::string name;
        Proposal::Kind kind;
    };

    GradientOptimizer(double lr,
                      std::vector<double> params,
                      std::vector<ParamSpec> spec);

    void observe(const Observation& o) override;
    [[nodiscard]] Proposal propose() const override;
    void apply(const Proposal& p) override;
    [[nodiscard]] std::string describe() const override;

    // Read-only parameter accessors (used by tests).
    [[nodiscard]] std::size_t size() const noexcept override;
    [[nodiscard]] double      param(std::size_t i) const;
    [[nodiscard]] const std::vector<double>& params() const noexcept;

private:
    double                              lr_;
    mutable std::vector<double>         params_;
    std::vector<ParamSpec>              spec_;
    mutable std::mutex                  mu_;
    mutable std::vector<double>         grad_buf_;     // accumulated grad
    mutable std::size_t                 grad_count_ = 0;
    mutable double                      last_loss_  = 0.0;
    mutable std::size_t                 step_count_ = 0;
    std::vector<double>                 momentum_;     // simple SGD w/ momentum
};

// Factory: build a GradientOptimizer that emits SetWorkerCount
// proposals for a single parameter (the worker count).
std::unique_ptr<Optimizer>
make_worker_count_optimizer(std::int64_t initial, double lr);

}  // namespace neuro::learn